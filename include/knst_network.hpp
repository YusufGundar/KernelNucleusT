// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/* ============================================================================
 * knst_network.hpp — KernelNucleusT network module
 * ----------------------------------------------------------------------------
 *  Layers : error model · IP/endpoint · DNS (sync/async/cache) · TCP/UDP/Unix
 *           sockets · TLS (OpenSSL) · URL · HTTP/1.1 client
 *
 *  HTTP client features
 *    - http + https, redirects (RFC 3986 resolution), cookies, basic auth
 *    - gzip / deflate / "gzip, gzip" decoding with decompression-bomb guards
 *    - keep-alive connection pool (idempotent first-byte retry)
 *    - HTTP proxy (absolute-URI for http, CONNECT tunnel for https), NO_PROXY
 *    - async DNS with deadline + thread-safe LRU/TTL DNS cache
 *    - optional SSRF guard (options::allow_private_networks = false)
 *
 *  TLS:  define KNST_NETWORK_ENABLE_TLS and link -lssl -lcrypto
 *        (Windows: also crypt32; system root store is imported automatically
 *         unless KNST_NETWORK_NO_WIN_CERTSTORE is defined).
 *        Android: system CA dir /system/etc/security/cacerts is used.
 *        Custom CAs: knst_tls_set_ca_file() / knst_tls_set_ca_path() BEFORE
 *        the first TLS connection.
 *        TLS >= 1.2, certificate + hostname (or iPAddress SAN) verification is
 *        always on, compression and renegotiation are disabled.
 *
 *  Thread safety
 *    knst_dns_cache, knst_connection_pool : thread-safe.
 *    knst_http_client : one instance per thread (cookies/headers unlocked).
 *    sockets : one thread per socket.
 *
 *  Notes
 *    - The TLS handshake runs on a non-blocking socket under ONE overall
 *      deadline. After the handshake the socket is blocking and TLS reads /
 *      writes use SO_RCVTIMEO / SO_SNDTIMEO (per-syscall timeout); the HTTP
 *      client additionally enforces options::total_timeout_ms between calls.
 *    - On POSIX (except macOS, which uses SO_NOSIGPIPE) every TLS write and
 *      shutdown blocks SIGPIPE on the calling thread for its duration; the
 *      process-wide signal disposition is never modified.
 *    - Hostnames must be ASCII (IDN callers must convert to punycode first).
 * ============================================================================ */

#pragma once


#include <cstring>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <climits>
#include <cerrno>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>
#include <new>
#include <utility>

#if defined(KNST_NETWORK_ENABLE_JSON) && !defined(KNST_JSON_HPP)
    #error "KNST_NETWORK_ENABLE_JSON requires knst_json.hpp to be included first"
#endif

#ifndef KNST_FORCE_INLINE
    #if defined(_MSC_VER)
        #define KNST_FORCE_INLINE __forceinline
    #elif defined(__GNUC__) || defined(__clang__)
        #define KNST_FORCE_INLINE __attribute__((always_inline)) inline
    #else
        #define KNST_FORCE_INLINE inline
    #endif
#endif

#ifndef KNST_USING_PLATFORM_WINDOWS
    #if defined(_WIN32) || defined(_WIN64)
        #define KNST_USING_PLATFORM_WINDOWS 1
    #else
        #define KNST_USING_PLATFORM_WINDOWS 0
    #endif
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    #define KNST_NET_HAS_EXCEPTIONS 1
#else
    #define KNST_NET_HAS_EXCEPTIONS 0
#endif

/* ============================================================================
 * Platform includes
 * ============================================================================ */

#if KNST_USING_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #if __has_include(<mstcpip.h>)
        #include <mstcpip.h>
    #endif
    #if __has_include(<afunix.h>)
        #include <afunix.h>
        #define KNST_NETWORK_HAS_AF_UNIX_WIN 1
    #else
        #define KNST_NETWORK_HAS_AF_UNIX_WIN 0
    #endif
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    using knst_socket_handle_t = SOCKET;
    static constexpr knst_socket_handle_t KNST_INVALID_SOCKET =
        static_cast<knst_socket_handle_t>(~static_cast<knst_socket_handle_t>(0));
#else
    #define KNST_NETWORK_HAS_AF_UNIX_WIN 0
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/un.h>
    #include <sys/stat.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/ioctl.h>
    #include <signal.h>
    #include <pthread.h>
    #include <time.h>
    #include <errno.h>
    #include <string.h>
    #include <stdio.h>
    using knst_socket_handle_t = int;
    static constexpr knst_socket_handle_t KNST_INVALID_SOCKET = -1;
#endif

#ifndef AI_ADDRCONFIG
    #define AI_ADDRCONFIG 0
#endif
#ifndef AI_NUMERICSERV
    #define AI_NUMERICSERV 0
#endif
#ifndef NI_MAXHOST
    #define NI_MAXHOST 1025
#endif

#if !KNST_USING_PLATFORM_WINDOWS
    #ifdef MSG_NOSIGNAL
        #define KNST_MSG_NOSIGNAL MSG_NOSIGNAL
    #else
        #define KNST_MSG_NOSIGNAL 0
    #endif
#endif

#if defined(__ANDROID__)

#include <jni.h>
#include <sys/system_properties.h>

namespace knst_net_detail {
namespace android_jni {

    static JavaVM* g_vm = nullptr;
    static jobject g_activity = nullptr;

        inline void set_context(JavaVM* vm, jobject activity) {
        if (!vm) return;
        g_vm = vm;
        if (!activity) return;
        JNIEnv* env = nullptr;
        bool attached_here = false;
        if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
            vm->AttachCurrentThread(&env, nullptr);
            attached_here = true;
        }
        if (!env) { if (attached_here) vm->DetachCurrentThread(); return; }
        // Idempotent: same Java object → no-op.
        if (g_activity && env->IsSameObject(g_activity, activity)) {
            if (attached_here) vm->DetachCurrentThread();
            return;
        }
        if (g_activity) { env->DeleteGlobalRef(g_activity); g_activity = nullptr; }
        g_activity = env->NewGlobalRef(activity);
        if (attached_here) vm->DetachCurrentThread();
    }

    inline JNIEnv* get_env() {
        if (!g_vm) return nullptr;
        JNIEnv* env = nullptr;
        if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
            g_vm->AttachCurrentThread(&env, nullptr);
        return env;
    }


    inline bool get_system_proxy(char* out_buf, size_t buf_size) {
        JNIEnv* env = get_env();
        if (!env) return false;

        jclass sys_class = env->FindClass("java/lang/System");
        if (!sys_class) return false;

        jmethodID get_prop = env->GetStaticMethodID(sys_class, "getProperty","(Ljava/lang/String;)Ljava/lang/String;");
                                                     
        if (!get_prop) { env->DeleteLocalRef(sys_class); return false; }

        jstring key_host = env->NewStringUTF("http.proxyHost");
        jstring key_port = env->NewStringUTF("http.proxyPort");

        jstring host = (jstring)env->CallStaticObjectMethod(sys_class, get_prop, key_host);
        jstring port = (jstring)env->CallStaticObjectMethod(sys_class, get_prop, key_port);

        bool ok = false;
        if (host) {
            const char* h = env->GetStringUTFChars(host, nullptr);
            const char* p = port ? env->GetStringUTFChars(port, nullptr) : "";
            if (h && h[0]) {
                int n = ::snprintf(out_buf, buf_size, "%s:%s",
                                   h, (p && p[0]) ? p : "8080");
                ok = (n > 0 && (size_t)n < buf_size);
            }
            if (h) env->ReleaseStringUTFChars(host, h);
            if (port && p) env->ReleaseStringUTFChars(port, p);
        }

        if (host) env->DeleteLocalRef(host);
        if (port) env->DeleteLocalRef(port);
        env->DeleteLocalRef(key_host);
        env->DeleteLocalRef(key_port);
        env->DeleteLocalRef(sys_class);
        return ok;
    }

} // namespace android_jni
} // namespace knst_net_detail

#endif // __ANDROID__


/* ============================================================================
 * 1) knst_network_error
 * ============================================================================ */

enum class knst_network_error : uint16_t {
    None = 0,
    Unknown, InvalidArgument, NotSupported, Interrupted,
    NotConnected, AlreadyConnected, AlreadyOpen, NotOpen,
    AddressInUse, AddressNotAvailable, PermissionDenied, WouldBlock, InProgress,
    ConnectionRefused, ConnectionReset, ConnectionAborted, ConnectionTimedOut,
    HostUnreachable, NetworkUnreachable, NetworkDown, BrokenPipe, NotSocket,
    MessageTooLarge, MessageTooSmall, PartialWrite, BufferEmpty,
    Timeout,
    DnsFailure, DnsNoData, DnsTemporaryFailure, DnsHostNotFound,
    TlsHandshakeFailed, TlsCertificateInvalid, TlsNotSupported,
    HttpMalformedStatus, HttpMalformedHeaders, HttpMalformedBody,
    HttpTooManyRedirects, HttpUnsupportedEncoding, HttpUnsupportedTransfer,
    HttpParseUrl, HttpConnectionClosed,
    OutOfMemory, TooManyOpenFiles, ConnectionClosed, ResponseTooLarge,
    HttpInvalidHeader, ProtocolError,
    ProxyFailed, CallbackAborted,
    ForbiddenAddress            // blocked by the SSRF guard (allow_private_networks = false)
};

KNST_FORCE_INLINE const char* knst_network_error_string(knst_network_error e) noexcept {
    switch (e) {
        case knst_network_error::None:                    return "no error";
        case knst_network_error::Unknown:                 return "unknown error";
        case knst_network_error::InvalidArgument:         return "invalid argument";
        case knst_network_error::NotSupported:            return "not supported";
        case knst_network_error::Interrupted:             return "interrupted";
        case knst_network_error::NotConnected:            return "not connected";
        case knst_network_error::AlreadyConnected:        return "already connected";
        case knst_network_error::AlreadyOpen:             return "already open";
        case knst_network_error::NotOpen:                 return "socket not open";
        case knst_network_error::AddressInUse:            return "address already in use";
        case knst_network_error::AddressNotAvailable:     return "address not available";
        case knst_network_error::PermissionDenied:        return "permission denied";
        case knst_network_error::WouldBlock:              return "would block";
        case knst_network_error::InProgress:              return "operation in progress";
        case knst_network_error::ConnectionRefused:       return "connection refused";
        case knst_network_error::ConnectionReset:         return "connection reset";
        case knst_network_error::ConnectionAborted:       return "connection aborted";
        case knst_network_error::ConnectionTimedOut:      return "connection timed out";
        case knst_network_error::HostUnreachable:         return "host unreachable";
        case knst_network_error::NetworkUnreachable:      return "network unreachable";
        case knst_network_error::NetworkDown:             return "network is down";
        case knst_network_error::BrokenPipe:              return "broken pipe";
        case knst_network_error::NotSocket:               return "not a socket";
        case knst_network_error::MessageTooLarge:         return "message too large";
        case knst_network_error::MessageTooSmall:         return "message too small";
        case knst_network_error::PartialWrite:            return "partial write";
        case knst_network_error::BufferEmpty:             return "buffer empty";
        case knst_network_error::Timeout:                 return "operation timed out";
        case knst_network_error::DnsFailure:              return "dns lookup failure";
        case knst_network_error::DnsNoData:               return "dns: no data";
        case knst_network_error::DnsTemporaryFailure:     return "dns: temporary failure";
        case knst_network_error::DnsHostNotFound:         return "dns: host not found";
        case knst_network_error::TlsHandshakeFailed:      return "tls handshake failed";
        case knst_network_error::TlsCertificateInvalid:   return "tls certificate invalid";
        case knst_network_error::TlsNotSupported:         return "tls not supported (recompile with KNST_NETWORK_ENABLE_TLS)";
        case knst_network_error::HttpMalformedStatus:     return "http: malformed status line";
        case knst_network_error::HttpMalformedHeaders:    return "http: malformed headers";
        case knst_network_error::HttpMalformedBody:       return "http: malformed body";
        case knst_network_error::HttpTooManyRedirects:    return "http: too many redirects";
        case knst_network_error::HttpUnsupportedEncoding: return "http: unsupported content-encoding";
        case knst_network_error::HttpUnsupportedTransfer: return "http: unsupported transfer-encoding";
        case knst_network_error::HttpParseUrl:            return "http: cannot parse url";
        case knst_network_error::HttpConnectionClosed:    return "http: connection closed unexpectedly";
        case knst_network_error::OutOfMemory:             return "out of memory";
        case knst_network_error::TooManyOpenFiles:        return "too many open files";
        case knst_network_error::ConnectionClosed:        return "connection closed";
        case knst_network_error::ResponseTooLarge:        return "response too large";
        case knst_network_error::HttpInvalidHeader:       return "http: invalid header";
        case knst_network_error::ProtocolError:           return "protocol error";
        case knst_network_error::ProxyFailed:             return "proxy failure";
        case knst_network_error::CallbackAborted:         return "aborted by body callback";
        case knst_network_error::ForbiddenAddress:        return "destination address is not allowed";
    }
    return "unknown error";
}


/* ============================================================================
 * 2) knst_net_detail — shared helpers
 * ============================================================================ */

namespace knst_net_detail {

    inline constexpr uint32_t KNST_NET_INFINITE = 0xFFFFFFFFu;
    inline constexpr size_t   KNST_NET_MAX_LEN  = 0xFFFFFFF0u;   // knst_*string uint32 limit

    inline void winsock_global_init() noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        static std::once_flag s_flag;
        std::call_once(s_flag, []() noexcept {
            WSADATA wsa{};
            ::WSAStartup(MAKEWORD(2, 2), &wsa);
        });
    #endif
    }

    KNST_FORCE_INLINE void close_handle(knst_socket_handle_t h) noexcept {
        if (h == KNST_INVALID_SOCKET) return;
    #if KNST_USING_PLATFORM_WINDOWS
        ::closesocket(h);
    #else
        ::close(h);
    #endif
    }

    KNST_FORCE_INLINE int last_socket_error() noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        return ::WSAGetLastError();
    #else
        return errno;
    #endif
    }

    KNST_FORCE_INLINE void set_blocking(knst_socket_handle_t h, bool blocking) noexcept {
        if (h == KNST_INVALID_SOCKET) return;
    #if KNST_USING_PLATFORM_WINDOWS
        u_long mode = blocking ? 0UL : 1UL;
        ::ioctlsocket(h, FIONBIO, &mode);
    #else
        int flags = ::fcntl(h, F_GETFL, 0);
        if (flags < 0) return;
        if (blocking) ::fcntl(h, F_SETFL, flags & ~O_NONBLOCK);
        else          ::fcntl(h, F_SETFL, flags |  O_NONBLOCK);
    #endif
    }

    // Marks an accepted/adopted handle close-on-exec / non-inheritable.
    KNST_FORCE_INLINE void set_no_inherit(knst_socket_handle_t h) noexcept {
        if (h == KNST_INVALID_SOCKET) return;
    #if KNST_USING_PLATFORM_WINDOWS
        ::SetHandleInformation(reinterpret_cast<HANDLE>(h), HANDLE_FLAG_INHERIT, 0);
    #else
        ::fcntl(h, F_SETFD, FD_CLOEXEC);
    #endif
    }

    // 0 or KNST_NET_INFINITE => no timeout
    inline void set_so_timeout(knst_socket_handle_t h, int optname, uint32_t ms) noexcept {
        if (h == KNST_INVALID_SOCKET) return;
        if (ms == KNST_NET_INFINITE) ms = 0;
    #if KNST_USING_PLATFORM_WINDOWS
        DWORD t = static_cast<DWORD>(ms);
        ::setsockopt(h, SOL_SOCKET, optname, reinterpret_cast<const char*>(&t), sizeof(t));
    #else
        struct timeval tv {};
        tv.tv_sec  = static_cast<time_t>(ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
        ::setsockopt(h, SOL_SOCKET, optname, &tv, sizeof(tv));
    #endif
    }
    inline void set_so_recv_timeout(knst_socket_handle_t h, uint32_t ms) noexcept {
        set_so_timeout(h, SO_RCVTIMEO, ms);
    }
    inline void set_so_send_timeout(knst_socket_handle_t h, uint32_t ms) noexcept {
        set_so_timeout(h, SO_SNDTIMEO, ms);
    }
    inline void set_so_timeouts(knst_socket_handle_t h, uint32_t recv_ms, uint32_t send_ms) noexcept {
        set_so_recv_timeout(h, recv_ms);
        set_so_send_timeout(h, send_ms);
    }

    inline knst_network_error map_errno(int e) noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        switch (e) {
            case 0:                    return knst_network_error::None;
            case WSAEWOULDBLOCK:       return knst_network_error::WouldBlock;
            case WSAEINPROGRESS:       return knst_network_error::InProgress;
            case WSAEALREADY:          return knst_network_error::InProgress;
            case WSAEINTR:             return knst_network_error::Interrupted;
            case WSAEINVAL:            return knst_network_error::InvalidArgument;
            case WSAEADDRINUSE:        return knst_network_error::AddressInUse;
            case WSAEADDRNOTAVAIL:     return knst_network_error::AddressNotAvailable;
            case WSAEACCES:            return knst_network_error::PermissionDenied;
            case WSAECONNREFUSED:      return knst_network_error::ConnectionRefused;
            case WSAECONNRESET:        return knst_network_error::ConnectionReset;
            case WSAECONNABORTED:      return knst_network_error::ConnectionAborted;
            case WSAETIMEDOUT:         return knst_network_error::ConnectionTimedOut;
            case WSAEHOSTUNREACH:      return knst_network_error::HostUnreachable;
            case WSAENETUNREACH:       return knst_network_error::NetworkUnreachable;
            case WSAENETDOWN:          return knst_network_error::NetworkDown;
            case WSAENETRESET:         return knst_network_error::ConnectionReset;
            case WSAENOTSOCK:          return knst_network_error::NotSocket;
            case WSAEMSGSIZE:          return knst_network_error::MessageTooLarge;
            case WSAENOTCONN:          return knst_network_error::NotConnected;
            case WSAEISCONN:           return knst_network_error::AlreadyConnected;
            case WSAENOBUFS:           return knst_network_error::OutOfMemory;
            case WSAEMFILE:            return knst_network_error::TooManyOpenFiles;
            case WSAESHUTDOWN:         return knst_network_error::BrokenPipe;
            case WSAEAFNOSUPPORT:
            case WSAEPROTONOSUPPORT:   return knst_network_error::NotSupported;
            default:                   return knst_network_error::Unknown;
        }
    #else
        switch (e) {
            case 0:                    return knst_network_error::None;
            case EAGAIN:               return knst_network_error::WouldBlock;
            #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
            case EWOULDBLOCK:          return knst_network_error::WouldBlock;
            #endif
            case EINPROGRESS:          return knst_network_error::InProgress;
            case EALREADY:             return knst_network_error::InProgress;
            case EINTR:                return knst_network_error::Interrupted;
            case EINVAL:               return knst_network_error::InvalidArgument;
            case EADDRINUSE:           return knst_network_error::AddressInUse;
            case EADDRNOTAVAIL:        return knst_network_error::AddressNotAvailable;
            case EACCES:
            case EPERM:                return knst_network_error::PermissionDenied;
            case ECONNREFUSED:         return knst_network_error::ConnectionRefused;
            case ECONNRESET:           return knst_network_error::ConnectionReset;
            case ECONNABORTED:         return knst_network_error::ConnectionAborted;
            case ETIMEDOUT:            return knst_network_error::ConnectionTimedOut;
            case EHOSTUNREACH:         return knst_network_error::HostUnreachable;
            case ENETUNREACH:          return knst_network_error::NetworkUnreachable;
            case ENETDOWN:             return knst_network_error::NetworkDown;
            case ENETRESET:            return knst_network_error::ConnectionReset;
            case EPIPE:                return knst_network_error::BrokenPipe;
            case ENOTSOCK:             return knst_network_error::NotSocket;
            case EMSGSIZE:             return knst_network_error::MessageTooLarge;
            case ENOTCONN:             return knst_network_error::NotConnected;
            case EISCONN:              return knst_network_error::AlreadyConnected;
            case ENOBUFS:
            case ENOMEM:               return knst_network_error::OutOfMemory;
            case EMFILE:
            case ENFILE:               return knst_network_error::TooManyOpenFiles;
            case EAFNOSUPPORT:
            case EPROTONOSUPPORT:
            case ESOCKTNOSUPPORT:      return knst_network_error::NotSupported;
            default:                   return knst_network_error::Unknown;
        }
    #endif
    }

    struct deadline {
        std::chrono::steady_clock::time_point end{};
        bool infinite = true;

        static deadline after(uint32_t ms) noexcept {
            deadline d;
            if (ms != KNST_NET_INFINITE) {
                d.infinite = false;
                d.end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            }
            return d;
        }

        uint32_t remaining_ms(bool& expired) const noexcept {
            expired = false;
            if (infinite) return KNST_NET_INFINITE;
            auto now = std::chrono::steady_clock::now();
            if (now >= end) { expired = true; return 0; }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
            if (ms < 1) ms = 1;
            if (ms > static_cast<long long>(KNST_NET_INFINITE - 1)) ms = static_cast<long long>(KNST_NET_INFINITE - 1);
            return static_cast<uint32_t>(ms);
        }
    };

    KNST_FORCE_INLINE uint32_t resolve_timeout(uint32_t requested, uint32_t configured) noexcept {
        if (requested != 0) return requested;
        return configured == 0 ? KNST_NET_INFINITE : configured;
    }

    // Budget for one connect attempt when `left` addresses remain.
    inline uint32_t attempt_budget(uint32_t rem, uint32_t left) noexcept {
        if (rem == KNST_NET_INFINITE || left <= 1) return rem;
        uint32_t b = rem / 2;
        return b < 1 ? 1 : b;
    }

    // DNS timeout bounded by an overall deadline. 0 => synchronous/unbounded.
    inline uint32_t dns_budget(uint32_t dns_to, const deadline& dl) noexcept {
        bool exp = false;
        uint32_t rem = dl.remaining_ms(exp);
        if (exp) return 1;
        if (rem == KNST_NET_INFINITE) return dns_to;
        if (dns_to == 0 || dns_to > rem) return rem;
        return dns_to;
    }

    inline bool poll_wait(knst_socket_handle_t h, bool want_read,
                          const deadline& dl, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (h == KNST_INVALID_SOCKET) { err = knst_network_error::NotOpen; return false; }
        for (;;) {
            bool expired = false;
            uint32_t ms = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; return false; }
            int tmo = (ms == KNST_NET_INFINITE)
                    ? -1
                    : (ms > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(ms));
        #if KNST_USING_PLATFORM_WINDOWS
            WSAPOLLFD pfd{};
            pfd.fd = h;
            pfd.events = want_read ? POLLRDNORM : POLLWRNORM;
            int rc = ::WSAPoll(&pfd, 1, tmo);
            if (rc < 0) {
                int se = ::WSAGetLastError();
                if (se == WSAEINTR) continue;
                err = map_errno(se); return false;
            }
            const short ready = want_read ? POLLRDNORM : POLLWRNORM;
        #else
            struct pollfd pfd{};
            pfd.fd = h;
            pfd.events = want_read ? POLLIN : POLLOUT;
            int rc = ::poll(&pfd, 1, tmo);
            if (rc < 0) {
                if (errno == EINTR) continue;
                err = map_errno(errno); return false;
            }
            const short ready = want_read ? POLLIN : POLLOUT;
        #endif
            if (rc == 0) continue;     // loop re-checks the deadline
            if (pfd.revents & ready) return true;
            if (pfd.revents & POLLNVAL) { err = knst_network_error::NotSocket; return false; }
            if (pfd.revents & (POLLERR | POLLHUP)) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                ::getsockopt(h, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &sl);
                err = map_errno(soerr);
                if (err == knst_network_error::None) err = knst_network_error::ConnectionClosed;
                return false;
            }
            return true;
        }
    }

    // true => idle socket shows no pending data / EOF / error (safe to reuse)
    inline bool socket_looks_alive(knst_socket_handle_t h) noexcept {
        if (h == KNST_INVALID_SOCKET) return false;
    #if KNST_USING_PLATFORM_WINDOWS
        WSAPOLLFD pfd{};
        pfd.fd = h; pfd.events = POLLRDNORM;
        int rc = ::WSAPoll(&pfd, 1, 0);
    #else
        struct pollfd pfd{};
        pfd.fd = h; pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, 0);
    #endif
        return rc == 0;
    }

    inline knst_socket_handle_t create_socket(int family, int type, int protocol) noexcept {
        winsock_global_init();
    #if KNST_USING_PLATFORM_WINDOWS
        SOCKET s = ::WSASocketW(family, type, protocol, nullptr, 0,
                                WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        if (s == INVALID_SOCKET && ::WSAGetLastError() == WSAEINVAL)
            s = ::WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
    #else
        #ifdef SOCK_CLOEXEC
            int s = ::socket(family, type | SOCK_CLOEXEC, protocol);
        #else
            int s = ::socket(family, type, protocol);
            if (s >= 0) ::fcntl(s, F_SETFD, FD_CLOEXEC);
        #endif
        #ifdef SO_NOSIGPIPE
            if (s >= 0) { int one = 1; ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)); }
        #endif
    #endif
        if (s != KNST_INVALID_SOCKET) set_blocking(s, false);
        return s;
    }

    KNST_FORCE_INLINE bool ipv6_is_loopback(const in6_addr& a) noexcept {
        static const uint8_t loop[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        return ::memcmp(reinterpret_cast<const void*>(&a), loop, 16) == 0;
    }
    KNST_FORCE_INLINE bool ipv6_is_unspecified(const in6_addr& a) noexcept {
        static const uint8_t zero[16] = {};
        return ::memcmp(reinterpret_cast<const void*>(&a), zero, 16) == 0;
    }
    KNST_FORCE_INLINE bool ipv6_is_multicast(const in6_addr& a) noexcept {
        return reinterpret_cast<const uint8_t*>(&a)[0] == 0xFF;
    }

    inline bool has_scheme(const knst_c16string& s) noexcept {
        if (s.length() < 4) return false;
        char16_t c0 = s[0];
        if (!((c0 >= u'a' && c0 <= u'z') || (c0 >= u'A' && c0 <= u'Z'))) return false;
        uint32_t i = 1;
        while (i < s.length() && s[i] != u':') {
            char16_t c = s[i];
            if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                  (c >= u'0' && c <= u'9') || c == u'+' || c == u'-' || c == u'.'))
                return false;
            ++i;
        }
        return (i + 2 < s.length()) && s[i] == u':' && s[i+1] == u'/' && s[i+2] == u'/';
    }

    inline bool ieq(const knst_c16string& a, const knst_c16string& b) noexcept {
        if (a.length() != b.length()) return false;
        for (uint32_t i = 0; i < a.length(); ++i) {
            char16_t ca = a[i], cb = b[i];
            if (ca >= u'A' && ca <= u'Z') ca = static_cast<char16_t>(ca - u'A' + u'a');
            if (cb >= u'A' && cb <= u'Z') cb = static_cast<char16_t>(cb - u'A' + u'a');
            if (ca != cb) return false;
        }
        return true;
    }

    inline void append_uint(knst_c16string& s, uint32_t v) {
        char buf[16];
        int n = ::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
        for (int i = 0; i < n; ++i) s.append(static_cast<char16_t>(buf[i]));
    }

    inline knst_c16string trim16(const knst_c16string& s) {
        uint32_t a = 0, b = s.length();
        while (a < b && (s[a] == u' ' || s[a] == u'\t')) ++a;
        while (b > a && (s[b - 1] == u' ' || s[b - 1] == u'\t')) --b;
        return s.substr(a, b - a);
    }

    inline knst_c16string lower16(const knst_c16string& s) {
        knst_c16string r = s;
        for (uint32_t i = 0; i < r.length(); ++i) {
            char16_t c = r[i];
            if (c >= u'A' && c <= u'Z') r[i] = static_cast<char16_t>(c - u'A' + u'a');
        }
        return r;
    }

    // true for "1.2.3.4"-style or ':'-containing (IPv6) host strings.
    inline bool looks_like_ip_literal(const knst_c16string& h) noexcept {
        if (h.empty()) return false;
        bool all_num = true;
        const bool first_digit = (h[0] >= u'0' && h[0] <= u'9');
        for (uint32_t i = 0; i < h.length(); ++i) {
            char16_t c = h[i];
            if (c == u':') return true;
            bool numeric = (c >= u'0' && c <= u'9') || c == u'.';
            bool hexish  = first_digit && ((c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F') || c == u'x' || c == u'X');
            if (!numeric && !hexish) all_num = false;
        }
        return all_num;
    }

    // Surrogate-pair-safe UTF-8 encoder
    inline knst_byte_string to_utf8(const knst_c16string& in) {
        knst_byte_string out;
        uint32_t i = 0;
        while (i < in.length()) {
            uint32_t cp = in[i++];
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i < in.length()) {
                    uint32_t lo = in[i];
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        ++i;
                    } else cp = 0xFFFD;
                } else cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            if (cp < 0x80) out.push_back(static_cast<uint8_t>(cp));
            else if (cp < 0x800) {
                out.push_back(static_cast<uint8_t>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<uint8_t>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<uint8_t>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    // Percent-encode a request target (preserves existing %XX)
    inline knst_c16string encode_target(const knst_c16string& in) {
        static const char* hex = "0123456789ABCDEF";
        auto is_hex = [](uint8_t c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        };
        knst_byte_string u8 = to_utf8(in);
        knst_c16string r;
        uint32_t i = 0;
        while (i < u8.length()) {
            uint8_t b = u8[i];
            if (b == '%' && i + 2 < u8.length() && is_hex(u8[i+1]) && is_hex(u8[i+2])) {
                r.append(u'%');
                r.append(static_cast<char16_t>(u8[i+1]));
                r.append(static_cast<char16_t>(u8[i+2]));
                i += 3; continue;
            }
            bool ok = (b > 0x20 && b < 0x7F) &&
                      b != '"' && b != '<' && b != '>' &&
                      b != '\\' && b != '^' && b != '`' &&
                      b != '{' && b != '|' && b != '}';
            if (ok) r.append(static_cast<char16_t>(b));
            else {
                r.append(u'%');
                r.append(static_cast<char16_t>(hex[b >> 4]));
                r.append(static_cast<char16_t>(hex[b & 0x0F]));
            }
            ++i;
        }
        return r;
    }

    inline bool is_tchar(char16_t c) noexcept {
        if (c >= u'0' && c <= u'9') return true;
        if (c >= u'a' && c <= u'z') return true;
        if (c >= u'A' && c <= u'Z') return true;
        switch (c) {
            case u'!': case u'#': case u'$': case u'%': case u'&': case u'\'':
            case u'*': case u'+': case u'-': case u'.': case u'^': case u'_':
            case u'`': case u'|': case u'~': return true;
            default: return false;
        }
    }
    inline bool valid_header_name(const knst_c16string& s) noexcept {
        if (s.empty()) return false;
        for (uint32_t i = 0; i < s.length(); ++i) if (!is_tchar(s[i])) return false;
        return true;
    }
    inline bool valid_header_value(const knst_c16string& s) noexcept {
        for (uint32_t i = 0; i < s.length(); ++i) {
            char16_t c = s[i];
            if (c == 0 || c == u'\r' || c == u'\n') return false;
        }
        return true;
    }

    inline bool get_env(const char* name, char* buf, size_t cap) noexcept {
        if (!name || !buf || cap < 2) return false;
    #if KNST_USING_PLATFORM_WINDOWS
        DWORD n = ::GetEnvironmentVariableA(name, buf, static_cast<DWORD>(cap));
        return n > 0 && n < cap;
    #else
        const char* v = ::getenv(name);
        if (!v || !*v) return false;
        size_t l = ::strlen(v);
        if (l >= cap) return false;
        ::memcpy(buf, v, l + 1);
        return true;
    #endif
    }

    inline knst_c16string ascii_to_c16(const char* s) {
        knst_c16string r;
        for (; s && *s; ++s) r.append(static_cast<char16_t>(static_cast<unsigned char>(*s)));
        return r;
    }

    inline knst_c16string base64_encode_bytes(const knst_byte_string& in) {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        knst_c16string out;
        uint32_t i = 0, n = in.length();
        while (i + 2 < n) {
            uint32_t v = (static_cast<uint32_t>(in[i])     << 16) |
                         (static_cast<uint32_t>(in[i + 1]) << 8)  |
                          static_cast<uint32_t>(in[i + 2]);
            out.append(static_cast<char16_t>(tbl[(v >> 18) & 0x3F]));
            out.append(static_cast<char16_t>(tbl[(v >> 12) & 0x3F]));
            out.append(static_cast<char16_t>(tbl[(v >>  6) & 0x3F]));
            out.append(static_cast<char16_t>(tbl[ v        & 0x3F]));
            i += 3;
        }
        if (i < n) {
            uint32_t rem = n - i;
            uint32_t v = static_cast<uint32_t>(in[i]) << 16;
            if (rem == 2) v |= static_cast<uint32_t>(in[i + 1]) << 8;
            out.append(static_cast<char16_t>(tbl[(v >> 18) & 0x3F]));
            out.append(static_cast<char16_t>(tbl[(v >> 12) & 0x3F]));
            out.append(rem == 2 ? static_cast<char16_t>(tbl[(v >> 6) & 0x3F]) : u'=');
            out.append(u'=');
        }
        return out;
    }

    inline knst_byte_string percent_decode_bytes(const knst_byte_string& in) {
        auto hv = [](uint8_t c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        knst_byte_string out;
        for (uint32_t i = 0; i < in.length(); ++i) {
            if (in[i] == '%' && i + 2 < in.length() && hv(in[i+1]) >= 0 && hv(in[i+2]) >= 0) {
                out.push_back(static_cast<uint8_t>(hv(in[i+1]) * 16 + hv(in[i+2])));
                i += 2;
            } else out.push_back(in[i]);
        }
        return out;
    }

    inline bool append_bytes(knst_byte_string& d, const uint8_t* p, size_t n) noexcept {
        if (n == 0) return true;
        size_t old = d.length();
        if (old + n > KNST_NET_MAX_LEN) return false;
        d.resize(static_cast<uint32_t>(old + n));
        if (d.length() != old + n) return false;
        ::memcpy(&d[static_cast<uint32_t>(old)], p, n);
        return true;
    }

    /* ------------------------------------------------------------------
     * Growable output buffer with hard limit (used by inflate + body readers)
     * ------------------------------------------------------------------ */
    struct byte_sink {
        knst_byte_string& buf;
        uint8_t* ptr = nullptr;   // cached buf.data() (valid after room()/ctor)
        size_t len = 0;           // logical length
        size_t cap = 0;           // allocated (buf.length())
        size_t limit;             // max logical length
        size_t base = 0;          // start of current member (window start)
        size_t soft = 0;          // min(cap, limit): fast-path bound
        bool   too_large = false;
        bool   oom = false;

        byte_sink(knst_byte_string& b, size_t lim) noexcept
            : buf(b), limit(lim > KNST_NET_MAX_LEN ? KNST_NET_MAX_LEN : lim) {
            len = cap = b.length();
            base = len;
            ptr = len ? &b[0] : nullptr;
            update_soft();
        }
        void update_soft() noexcept { soft = cap < limit ? cap : limit; }

        bool room(size_t extra) noexcept {
            if (extra > limit || len > limit - extra) { too_large = true; return false; }
            size_t need = len + extra;
            if (need <= cap) return true;
            size_t nc = cap + cap / 2;
            if (nc < need) nc = need;
            if (nc < 4096) nc = 4096;
            if (nc > KNST_NET_MAX_LEN) nc = KNST_NET_MAX_LEN;
            buf.resize(static_cast<uint32_t>(nc));
            if (buf.length() != nc) { oom = true; return false; }
            cap = nc;
            ptr = &buf[0];
            update_soft();
            return true;
        }
        bool put(uint8_t b) noexcept {
            if (len < soft) { ptr[len++] = b; return true; }
            if (!room(1)) return false;
            ptr[len++] = b;
            return true;
        }
        uint8_t* at(size_t i) noexcept { return ptr + i; }
        void finish() noexcept {
            buf.resize(static_cast<uint32_t>(len));
            cap = len;
                        ptr = len ? &buf[0] : nullptr;
            update_soft();
        }
        knst_network_error error() const noexcept {
            return oom ? knst_network_error::OutOfMemory : knst_network_error::ResponseTooLarge;
        }
    };

    inline uint32_t le32(const uint8_t* p) noexcept {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    inline uint32_t be32(const uint8_t* p) noexcept {
        return static_cast<uint32_t>(p[3]) | (static_cast<uint32_t>(p[2]) << 8) |
               (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[0]) << 24);
    }

    /* ------------------------------------------------------------------
     * CRC32 / Adler32
     * ------------------------------------------------------------------ */
    struct crc_table {
        uint32_t t[8][256];
        crc_table() noexcept {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[0][i] = c;
            }
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = t[0][i];
                for (int k = 1; k < 8; ++k) { c = t[0][c & 0xFF] ^ (c >> 8); t[k][i] = c; }
            }
        }
    };
    inline uint32_t crc32_calc(const uint8_t* p, size_t n) noexcept {
        static const crc_table tb;
        uint32_t c = 0xFFFFFFFFu;
        while (n >= 8) {                       // slicing-by-8 (endian-independent)
            uint32_t a0 = le32(p) ^ c;
            uint32_t b0 = le32(p + 4);
            c = tb.t[7][a0 & 0xFF] ^ tb.t[6][(a0 >> 8) & 0xFF] ^
                tb.t[5][(a0 >> 16) & 0xFF] ^ tb.t[4][a0 >> 24] ^
                tb.t[3][b0 & 0xFF] ^ tb.t[2][(b0 >> 8) & 0xFF] ^
                tb.t[1][(b0 >> 16) & 0xFF] ^ tb.t[0][b0 >> 24];
            p += 8; n -= 8;
        }
        while (n--) c = tb.t[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }
    inline uint32_t adler32_calc(const uint8_t* p, size_t n) noexcept {
        uint32_t a = 1, b = 0;
        while (n > 0) {
            size_t k = n < 5552 ? n : 5552;
            for (size_t i = 0; i < k; ++i) { a += p[i]; b += a; }
            a %= 65521u; b %= 65521u;
            p += k; n -= k;
        }
        return (b << 16) | a;
    }

    /* ------------------------------------------------------------------
     * DEFLATE (RFC 1951) — bounded, allocation-safe inflate
     * ------------------------------------------------------------------ */
    enum { INF_OK = 0, INF_BAD = 1, INF_TOO_BIG = 2, INF_OOM = 3 };

    #if (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) \
        || defined(_WIN32) || defined(__LITTLE_ENDIAN__)
        #define KNST_NET_LITTLE_ENDIAN 1
    #else
        #define KNST_NET_LITTLE_ENDIAN 0
    #endif

    struct inflate_ctx {
        const uint8_t* in = nullptr;
        size_t   inlen = 0, incnt = 0;
        uint64_t bitbuf = 0;
        int      bitcnt = 0;
        byte_sink* out = nullptr;
    };
    // 10-bit first-level table + canonical fallback for longer codes
    struct inf_huff { uint16_t fast[1024]; int16_t count[16]; int16_t symbol[288]; };

    inline void inf_refill(inflate_ctx& s) noexcept {
    #if KNST_NET_LITTLE_ENDIAN
        if (s.incnt + 8 <= s.inlen) {
            uint64_t w;
            ::memcpy(&w, s.in + s.incnt, 8);
            s.bitbuf |= w << s.bitcnt;
            int adv = (63 - s.bitcnt) >> 3;
            s.incnt += static_cast<size_t>(adv);
            s.bitcnt += adv * 8;
            return;
        }
    #endif
        while (s.bitcnt <= 56 && s.incnt < s.inlen) {
            s.bitbuf |= static_cast<uint64_t>(s.in[s.incnt++]) << s.bitcnt;
            s.bitcnt += 8;
        }
    }

    inline int inf_bits(inflate_ctx& s, int need) noexcept {
        if (s.bitcnt < need) {
            inf_refill(s);
            if (s.bitcnt < need) return -1;
        }
        int v = static_cast<int>(s.bitbuf & ((1ull << need) - 1ull));
        s.bitbuf >>= need;
        s.bitcnt -= need;
        return v;
    }

    inline int inf_construct(inf_huff& h, const int16_t* length, int n) noexcept {
        int16_t offs[16];
        for (int len = 0; len <= 15; ++len) h.count[len] = 0;
        for (int sym = 0; sym < n; ++sym) h.count[length[sym]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<int16_t>(offs[len] + h.count[len]);
        for (int sym = 0; sym < n; ++sym)
            if (length[sym] != 0) h.symbol[offs[length[sym]]++] = static_cast<int16_t>(sym);
        return left;
    }

    inline uint32_t inf_reverse(uint32_t code, int len) noexcept {
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) { r = (r << 1) | (code & 1u); code >>= 1; }
        return r;
    }

    // construct + build the fast table. Returns like inf_construct.
    inline int inf_make(inf_huff& h, const int16_t* length, int n) noexcept {
        int left = inf_construct(h, length, n);
        ::memset(h.fast, 0, sizeof(h.fast));
        if (left < 0 || h.count[0] == n) return left;
        int offs[16]; offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
        uint32_t code = 0, prev = 0;
        for (int len = 1; len <= 10; ++len) {
            code = (code + prev) << 1;
            prev = static_cast<uint32_t>(h.count[len]);
            for (int k = 0; k < h.count[len]; ++k) {
                uint32_t sym = static_cast<uint32_t>(h.symbol[offs[len] + k]);
                uint32_t rev = inf_reverse(code + static_cast<uint32_t>(k), len);
                uint16_t entry = static_cast<uint16_t>((len << 9) | sym);
                for (uint32_t idx = rev; idx < 1024u; idx += (1u << len)) h.fast[idx] = entry;
            }
        }
        return left;
    }

    inline int inf_decode(inflate_ctx& s, const inf_huff& h) noexcept {
        if (s.bitcnt < 15) inf_refill(s);
        uint16_t e = h.fast[s.bitbuf & 1023u];
        if (e) {
            int len = e >> 9;
            if (len > s.bitcnt) return -1;
            s.bitbuf >>= len; s.bitcnt -= len;
            return e & 511;
        }
        int code = 0, first = 0, index = 0;      // canonical (rare: codes > 10 bits)
        uint64_t b = s.bitbuf;
        for (int len = 1; len <= 15; ++len) {
            code |= static_cast<int>(b & 1u); b >>= 1;
            int count = h.count[len];
            if (code - count < first) {
                if (len > s.bitcnt) return -1;
                s.bitbuf >>= len; s.bitcnt -= len;
                return h.symbol[index + (code - first)];
            }
            index += count; first += count; first <<= 1; code <<= 1;
        }
        return -2;
    }

    inline int inf_fail_rc(const byte_sink& o) noexcept { return o.oom ? INF_OOM : INF_TOO_BIG; }

    inline int inf_codes(inflate_ctx& s, const inf_huff& lc, const inf_huff& dc) noexcept {
        static const int16_t lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const int16_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const int16_t dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const int16_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        byte_sink& o = *s.out;
        for (;;) {
            if (s.bitcnt < 48) inf_refill(s);
            int sym = inf_decode(s, lc);
            if (sym < 0) return INF_BAD;
            if (sym < 256) {
                if (o.len < o.soft) o.ptr[o.len++] = static_cast<uint8_t>(sym);
                else if (!o.put(static_cast<uint8_t>(sym))) return inf_fail_rc(o);
                // chain further literals without re-entering the generic path
                while (s.bitcnt >= 15 && o.len < o.soft) {
                    uint16_t e2 = lc.fast[s.bitbuf & 1023u];
                    if (!e2 || (e2 & 511) >= 256) break;
                    int l2 = e2 >> 9;
                    s.bitbuf >>= l2; s.bitcnt -= l2;
                    o.ptr[o.len++] = static_cast<uint8_t>(e2 & 511);
                }
                continue;
            }
            if (sym == 256) return INF_OK;
            sym -= 257;
            if (sym >= 29) return INF_BAD;
            int eb = lext[sym];
            if (s.bitcnt < eb) { inf_refill(s); if (s.bitcnt < eb) return INF_BAD; }
            size_t len = static_cast<size_t>(lens[sym]) + static_cast<size_t>(s.bitbuf & ((1ull << eb) - 1ull));
            s.bitbuf >>= eb; s.bitcnt -= eb;

            int ds = inf_decode(s, dc);
            if (ds < 0 || ds >= 30) return INF_BAD;
            int db = dext[ds];
            if (s.bitcnt < db) { inf_refill(s); if (s.bitcnt < db) return INF_BAD; }
            size_t dist = static_cast<size_t>(dists[ds]) + static_cast<size_t>(s.bitbuf & ((1ull << db) - 1ull));
            s.bitbuf >>= db; s.bitcnt -= db;

            if (dist > o.len - o.base) return INF_BAD;
            if (!o.room(len)) return inf_fail_rc(o);
            uint8_t* d = o.ptr + o.len;
            const uint8_t* src = d - dist;
            if (dist >= len)      ::memcpy(d, src, len);
            else if (dist == 1)   ::memset(d, *src, len);
            else                  for (size_t i = 0; i < len; ++i) d[i] = src[i];
            o.len += len;
        }
    }

    inline int inf_fixed(inflate_ctx& s) noexcept {
        struct fixed_tables {
            inf_huff lc, dc;
            fixed_tables() noexcept {
                int16_t lengths[288];
                int sym = 0;
                for (; sym < 144; ++sym) lengths[sym] = 8;
                for (; sym < 256; ++sym) lengths[sym] = 9;
                for (; sym < 280; ++sym) lengths[sym] = 7;
                for (; sym < 288; ++sym) lengths[sym] = 8;
                inf_make(lc, lengths, 288);
                for (sym = 0; sym < 30; ++sym) lengths[sym] = 5;
                inf_make(dc, lengths, 30);
            }
        };
        static const fixed_tables ft;              // thread-safe one-time init
        return inf_codes(s, ft.lc, ft.dc);
    }

    inline int inf_dynamic(inflate_ctx& s) noexcept {
        static const int16_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        int16_t lengths[320];
        inf_huff lc, dc;
        int nlen = inf_bits(s, 5), ndist = inf_bits(s, 5), ncode = inf_bits(s, 4);
        if (nlen < 0 || ndist < 0 || ncode < 0) return INF_BAD;
        nlen += 257; ndist += 1; ncode += 4;
        if (nlen > 286 || ndist > 30) return INF_BAD;
        int idx = 0;
        for (; idx < ncode; ++idx) {
            int b = inf_bits(s, 3);
            if (b < 0) return INF_BAD;
            lengths[order[idx]] = static_cast<int16_t>(b);
        }
        for (; idx < 19; ++idx) lengths[order[idx]] = 0;
        if (inf_make(lc, lengths, 19) != 0) return INF_BAD;
        idx = 0;
        while (idx < nlen + ndist) {
            int sym = inf_decode(s, lc);
            if (sym < 0) return INF_BAD;
            if (sym < 16) {
                lengths[idx++] = static_cast<int16_t>(sym);
            } else {
                int len = 0, rep = 0, b = 0;
                if (sym == 16) {
                    if (idx == 0) return INF_BAD;
                    len = lengths[idx - 1];
                    b = inf_bits(s, 2); if (b < 0) return INF_BAD;
                    rep = 3 + b;
                } else if (sym == 17) {
                    b = inf_bits(s, 3); if (b < 0) return INF_BAD;
                    rep = 3 + b;
                } else {
                    b = inf_bits(s, 7); if (b < 0) return INF_BAD;
                    rep = 11 + b;
                }
                if (idx + rep > nlen + ndist) return INF_BAD;
                while (rep--) lengths[idx++] = static_cast<int16_t>(len);
            }
        }
        if (lengths[256] == 0) return INF_BAD;
        int err = inf_make(lc, lengths, nlen);
        if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return INF_BAD;
        err = inf_make(dc, lengths + nlen, ndist);
        if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return INF_BAD;
        return inf_codes(s, lc, dc);
    }

    inline int inf_stored(inflate_ctx& s) noexcept {
        int drop = s.bitcnt & 7;                       // discard partial byte
        s.bitbuf >>= drop; s.bitcnt -= drop;
        s.incnt -= static_cast<size_t>(s.bitcnt / 8);  // give back whole buffered bytes
        s.bitbuf = 0; s.bitcnt = 0;
        if (s.incnt + 4 > s.inlen) return INF_BAD;
        uint32_t len  = s.in[s.incnt] | (static_cast<uint32_t>(s.in[s.incnt + 1]) << 8);
        uint32_t nlen = s.in[s.incnt + 2] | (static_cast<uint32_t>(s.in[s.incnt + 3]) << 8);
        s.incnt += 4;
        if (len != (~nlen & 0xFFFFu)) return INF_BAD;
        if (s.incnt + len > s.inlen) return INF_BAD;
        if (len > 0) {
            byte_sink& o = *s.out;
            if (!o.room(len)) return inf_fail_rc(o);
            ::memcpy(o.ptr + o.len, s.in + s.incnt, len);
            o.len += len;
            s.incnt += len;
        }
        return INF_OK;
    }

    // Decodes one raw DEFLATE stream; `consumed` = input bytes used.
    inline int inflate_raw(const uint8_t* in, size_t inlen, byte_sink& out, size_t& consumed) noexcept {
        inflate_ctx s;
        s.in = in; s.inlen = inlen; s.out = &out;
        int last;
        do {
            last = inf_bits(s, 1);
            if (last < 0) return INF_BAD;
            int type = inf_bits(s, 2);
            if (type < 0) return INF_BAD;
            int rc;
            switch (type) {
                case 0:  rc = inf_stored(s);  break;
                case 1:  rc = inf_fixed(s);   break;
                case 2:  rc = inf_dynamic(s); break;
                default: rc = INF_BAD;        break;
            }
            if (rc != INF_OK) return rc;
        } while (!last);
        consumed = s.incnt - static_cast<size_t>(s.bitcnt / 8);
        return INF_OK;
    }

    inline knst_network_error map_inflate_rc(int rc) noexcept {
        switch (rc) {
            case INF_OK:      return knst_network_error::None;
            case INF_TOO_BIG: return knst_network_error::ResponseTooLarge;
            case INF_OOM:     return knst_network_error::OutOfMemory;
            default:          return knst_network_error::HttpMalformedBody;
        }
    }

    // limit = min(max_out, max(1 MiB, in_len*1000))  (ratio guard)
    inline size_t effective_inflate_limit(size_t in_len, size_t max_out) noexcept {
        size_t lim = max_out;
        if (in_len <= KNST_NET_MAX_LEN / 1000) {
            size_t ratio = in_len * 1000;
            if (ratio < (size_t(1) << 20)) ratio = size_t(1) << 20;
            if (ratio < lim) lim = ratio;
        }
        return lim;
    }

    // RFC 1952 gzip (multi-member supported)
    inline bool gzip_decode(const uint8_t* in, size_t n, knst_byte_string& out,
                            size_t max_out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();
        if (!in) { err = knst_network_error::InvalidArgument; return false; }
        byte_sink sink(out, effective_inflate_limit(n, max_out));
        size_t pos = 0;
        auto bad = [&](knst_network_error e) { err = e; out.clear(); return false; };
        for (;;) {
            if (n - pos < 18) return bad(knst_network_error::HttpMalformedBody);
            if (in[pos] != 0x1f || in[pos + 1] != 0x8b || in[pos + 2] != 8)
                return bad(knst_network_error::HttpMalformedBody);
            uint8_t flg = in[pos + 3];
            if (flg & 0xE0) return bad(knst_network_error::HttpMalformedBody);
            size_t p = pos + 10;
            if (flg & 4) {
                if (p + 2 > n) return bad(knst_network_error::HttpMalformedBody);
                size_t xlen = in[p] | (static_cast<size_t>(in[p + 1]) << 8);
                p += 2 + xlen;
                if (p > n) return bad(knst_network_error::HttpMalformedBody);
            }
            if (flg & 8)  { while (p < n && in[p]) ++p; if (p >= n) return bad(knst_network_error::HttpMalformedBody); ++p; }
            if (flg & 16) { while (p < n && in[p]) ++p; if (p >= n) return bad(knst_network_error::HttpMalformedBody); ++p; }
            if (flg & 2)  { p += 2; if (p > n) return bad(knst_network_error::HttpMalformedBody); }

            size_t start = sink.len;
            sink.base = start;
            size_t used = 0;
            int rc = inflate_raw(in + p, n - p, sink, used);
            if (rc != INF_OK) return bad(map_inflate_rc(rc));
            p += used;
            if (n - p < 8) return bad(knst_network_error::HttpMalformedBody);
            uint32_t crc   = le32(in + p);
            uint32_t isize = le32(in + p + 4);
            p += 8;
            uint32_t actual = (sink.len > start) ? crc32_calc(sink.at(start), sink.len - start) : crc32_calc(nullptr, 0);
            if (crc != actual || isize != static_cast<uint32_t>((sink.len - start) & 0xFFFFFFFFu))
                return bad(knst_network_error::HttpMalformedBody);
            pos = p;
            if (pos >= n) break;
            if (n - pos >= 2 && in[pos] == 0x1f && in[pos + 1] == 0x8b) continue;
            for (size_t i = pos; i < n; ++i)
                if (in[i] != 0) return bad(knst_network_error::HttpMalformedBody);
            break;
        }
        sink.finish();
        return true;
    }

    // zlib (RFC 1950) with raw-deflate fallback (some servers send raw)
    inline bool deflate_decode(const uint8_t* in, size_t n, knst_byte_string& out,
                               size_t max_out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();
        if (!in) { err = knst_network_error::InvalidArgument; return false; }
        if (n >= 6) {
            uint32_t cmf = in[0], flg = in[1];
            if ((cmf & 0x0F) == 8 && (cmf >> 4) <= 7 && ((cmf << 8) | flg) % 31 == 0 && !(flg & 0x20)) {
                byte_sink sink(out, effective_inflate_limit(n, max_out));
                size_t used = 0;
                int rc = inflate_raw(in + 2, n - 2, sink, used);
                if (rc == INF_OK && n - 2 - used >= 4 &&
                    be32(in + 2 + used) == (sink.len ? adler32_calc(sink.at(0), sink.len) : adler32_calc(nullptr, 0))) {
                    sink.finish();
                    return true;
                }
                if (rc == INF_TOO_BIG || rc == INF_OOM) {
                    err = map_inflate_rc(rc); out.clear(); return false;
                }
                out.clear();      // fall through: try raw deflate
            }
        }
        byte_sink sink(out, effective_inflate_limit(n, max_out));
        size_t used = 0;
        int rc = inflate_raw(in, n, sink, used);
        if (rc != INF_OK) { err = map_inflate_rc(rc); out.clear(); return false; }
        sink.finish();
        return true;
    }

    // Applies a Content-Encoding list (applied in order => decode in reverse).
    inline bool decode_content_encoding(const knst_c16string& ce, knst_byte_string& body,
                                        size_t max_out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        int kinds[4]; int count = 0;     // 0 identity, 1 gzip, 2 deflate
        uint32_t start = 0;
        for (uint32_t k = 0; k <= ce.length(); ++k) {
            if (k == ce.length() || ce[k] == u',') {
                knst_c16string tok = trim16(ce.substr(start, k - start));
                start = k + 1;
                if (tok.empty()) continue;
                int kind;
                if (ieq(tok, u"gzip") || ieq(tok, u"x-gzip")) kind = 1;
                else if (ieq(tok, u"deflate"))                kind = 2;
                else if (ieq(tok, u"identity"))               kind = 0;
                else { err = knst_network_error::HttpUnsupportedEncoding; return false; }
                if (count >= 4) { err = knst_network_error::HttpUnsupportedEncoding; return false; }
                kinds[count++] = kind;
            }
        }
        for (int i = count - 1; i >= 0; --i) {
            if (kinds[i] == 0) continue;
            knst_byte_string tmp;
            bool ok = (kinds[i] == 1)
                    ? gzip_decode(body.data(), body.length(), tmp, max_out, err)
                    : deflate_decode(body.data(), body.length(), tmp, max_out, err);
            if (!ok) return false;
            body = std::move(tmp);
        }
        return true;
    }

} // namespace knst_net_detail


/* ============================================================================
 * 3) knst_ip_address
 * ============================================================================ */

class knst_ip_address {
public:
    knst_ip_address() noexcept = default;

    static knst_ip_address from_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept {
        knst_ip_address r;
        auto* v4 = reinterpret_cast<sockaddr_in*>(&r.m_addr);
        v4->sin_family = AF_INET;
        uint8_t raw[4] = {a, b, c, d};
        ::memcpy(&v4->sin_addr, raw, 4);
        r.m_valid = true; r.m_family = AF_INET;
        return r;
    }

    static knst_ip_address from_ipv6(const uint8_t raw[16]) noexcept {
        knst_ip_address r;
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&r.m_addr);
        v6->sin6_family = AF_INET6;
        ::memcpy(&v6->sin6_addr, raw, 16);
        r.m_valid = true; r.m_family = AF_INET6;
        return r;
    }

    static knst_ip_address from_raw(const sockaddr* sa) noexcept {
        knst_ip_address r;
        if (!sa) return r;
        if (sa->sa_family == AF_INET) {
            ::memcpy(&r.m_addr, sa, sizeof(sockaddr_in));
            r.m_valid = true; r.m_family = AF_INET;
        } else if (sa->sa_family == AF_INET6) {
            ::memcpy(&r.m_addr, sa, sizeof(sockaddr_in6));
            r.m_valid = true; r.m_family = AF_INET6;
        }
        return r;
    }

        static knst_ip_address from_string(const knst_c16string& s, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        knst_net_detail::winsock_global_init();  // inet_pton requires Winsock on Windows
        knst_ip_address r;
        if (s.length() == 0 || s.length() >= 96) {
            err = knst_network_error::InvalidArgument; return r;
        }
        char ascii[96]; uint32_t n = 0;
        uint32_t zone_start = static_cast<uint32_t>(-1);
        for (uint32_t i = 0; i < s.length(); ++i) {
            char16_t c = s[i];
            if (c == u'%' && zone_start == static_cast<uint32_t>(-1)) {
                zone_start = n;   // remember position of '%', skip it
                continue;
            }
            if (c == 0 || c >= 128) { err = knst_network_error::InvalidArgument; return r; }
            ascii[n++] = static_cast<char>(c);
        }
        ascii[n] = '\0';

        // Split into address part + zone part (if any).
        char addr_ascii[64];
        char zone_name[64] = {};
        if (zone_start != static_cast<uint32_t>(-1)) {
            if (zone_start >= sizeof(addr_ascii)) { err = knst_network_error::InvalidArgument; return r; }
            ::memcpy(addr_ascii, ascii, zone_start);
            addr_ascii[zone_start] = '\0';
            uint32_t zlen = n - zone_start;
            if (zlen == 0 || zlen >= sizeof(zone_name)) {
                err = knst_network_error::InvalidArgument; return r;
            }
            ::memcpy(zone_name, ascii + zone_start, zlen);
        } else {
            if (n >= sizeof(addr_ascii)) { err = knst_network_error::InvalidArgument; return r; }
            ::memcpy(addr_ascii, ascii, n + 1);
        }

        sockaddr_in v4{}; v4.sin_family = AF_INET;
        if (::inet_pton(AF_INET, addr_ascii, &v4.sin_addr) == 1) {
            if (zone_name[0]) { err = knst_network_error::InvalidArgument; return r; }
            ::memcpy(&r.m_addr, &v4, sizeof(v4));
            r.m_valid = true; r.m_family = AF_INET; return r;
        }

        sockaddr_in6 v6{}; v6.sin6_family = AF_INET6;
        if (::inet_pton(AF_INET6, addr_ascii, &v6.sin6_addr) == 1) {
            if (zone_name[0]) {
                bool numeric = true;
                for (const char* p = zone_name; *p; ++p)
                    if (*p < '0' || *p > '9') { numeric = false; break; }
                uint32_t zid = 0;
                if (numeric) {
                    for (const char* p = zone_name; *p; ++p) {
                        zid = zid * 10 + static_cast<uint32_t>(*p - '0');
                        if (zid > 0xFFFFFFFFu) { numeric = false; break; }
                    }
                }
            #if !KNST_USING_PLATFORM_WINDOWS
                if (!numeric) {
                    unsigned idx = ::if_nametoindex(zone_name);
                    if (idx == 0) { err = knst_network_error::InvalidArgument; return r; }
                    zid = idx;
                }
            #else
                if (!numeric) { err = knst_network_error::InvalidArgument; return r; }
            #endif
                v6.sin6_scope_id = zid;
            }
            ::memcpy(&r.m_addr, &v6, sizeof(v6));
            r.m_valid = true; r.m_family = AF_INET6; return r;
        }
        err = knst_network_error::InvalidArgument;
        return r;
    }

    static knst_ip_address loopback_v4() noexcept { return from_ipv4(127,0,0,1); }
    static knst_ip_address any_v4()      noexcept { return from_ipv4(0,0,0,0); }
    static knst_ip_address loopback_v6() noexcept {
        uint8_t raw[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        return from_ipv6(raw);
    }
    static knst_ip_address any_v6() noexcept { uint8_t raw[16] = {}; return from_ipv6(raw); }

    bool is_valid()  const noexcept { return m_valid; }
    bool is_ipv4()   const noexcept { return m_valid && m_family == AF_INET; }
    bool is_ipv6()   const noexcept { return m_valid && m_family == AF_INET6; }
    int  family()    const noexcept { return m_family; }

    bool is_loopback() const noexcept {
        if (!m_valid) return false;
        if (m_family == AF_INET) {
            auto* v4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            return (ntohl(v4->sin_addr.s_addr) >> 24) == 127u;
        }
        if (m_family == AF_INET6) {
            auto* v6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(&v6->sin6_addr);
            static const uint8_t v4map[12] = {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF};
            if (::memcmp(raw, v4map, 12) == 0) return raw[12] == 127;
            return knst_net_detail::ipv6_is_loopback(v6->sin6_addr);
        }
        return false;
    }
    bool is_any() const noexcept {
        if (!m_valid) return false;
        if (m_family == AF_INET) {
            auto* v4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            return v4->sin_addr.s_addr == 0;
        }
        if (m_family == AF_INET6) {
            auto* v6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(&v6->sin6_addr);
            static const uint8_t v4map[12] = {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF};
            if (::memcmp(raw, v4map, 12) == 0)
                return raw[12] == 0 && raw[13] == 0 && raw[14] == 0 && raw[15] == 0;
            return knst_net_detail::ipv6_is_unspecified(v6->sin6_addr);
        }
        return false;
    }
    bool is_multicast() const noexcept {
        if (!m_valid) return false;
        if (m_family == AF_INET) {
            auto* v4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            return (ntohl(v4->sin_addr.s_addr) >> 28) == 0xEu;
        }
        if (m_family == AF_INET6) {
            auto* v6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(&v6->sin6_addr);
            static const uint8_t v4map[12] = {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF};
            if (::memcmp(raw, v4map, 12) == 0) return (raw[12] >> 4) == 0xE;
            return knst_net_detail::ipv6_is_multicast(v6->sin6_addr);
        }
        return false;
    }

    // true for any address that is not a normal public unicast address:
    // loopback, unspecified, multicast/broadcast, RFC1918, CGNAT (100.64/10),
    // link-local (169.254/16, fe80::/10), ULA (fc00::/7), 0.0.0.0/8, reserved
    // (240/4), documentation/benchmark ranges, and IPv4-mapped forms thereof.
    // Used by the SSRF guard (knst_http_client::options::allow_private_networks).
    bool is_non_public() const noexcept {
        if (!m_valid) return true;
        if (m_family == AF_INET) {
            auto* v4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            return v4_non_public(ntohl(v4->sin_addr.s_addr));
        }
        if (m_family == AF_INET6) {
            auto* v6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            const uint8_t* r = reinterpret_cast<const uint8_t*>(&v6->sin6_addr);
            static const uint8_t v4map[12] = {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF};
            if (::memcmp(r, v4map, 12) == 0) {
                uint32_t a = (static_cast<uint32_t>(r[12]) << 24) | (static_cast<uint32_t>(r[13]) << 16) |
                             (static_cast<uint32_t>(r[14]) << 8)  |  static_cast<uint32_t>(r[15]);
                return v4_non_public(a);
            }
            if (knst_net_detail::ipv6_is_loopback(v6->sin6_addr) ||
                knst_net_detail::ipv6_is_unspecified(v6->sin6_addr) ||
                knst_net_detail::ipv6_is_multicast(v6->sin6_addr)) return true;
            if ((r[0] & 0xFE) == 0xFC) return true;                    // fc00::/7 ULA
            if (r[0] == 0xFE && (r[1] & 0xC0) == 0x80) return true;    // fe80::/10 link-local
            if (r[0] == 0xFE && (r[1] & 0xC0) == 0xC0) return true;    // fec0::/10 site-local (deprecated)
            if (r[0] == 0x20 && r[1] == 0x01 && r[2] == 0x0D && r[3] == 0xB8) return true;  // 2001:db8::/32
            if (r[0] == 0x00 && r[1] == 0x64 && r[2] == 0xFF && r[3] == 0x9B) {             // 64:ff9b::/96 NAT64
                uint32_t a = (static_cast<uint32_t>(r[12]) << 24) | (static_cast<uint32_t>(r[13]) << 16) |
                             (static_cast<uint32_t>(r[14]) << 8)  |  static_cast<uint32_t>(r[15]);
                return v4_non_public(a);
            }
            if (r[0] == 0x20 && r[1] == 0x02) {                                       // 6to4: embedded v4 in bytes 2..5
                uint32_t a = (uint32_t(r[2]) << 24) | (uint32_t(r[3]) << 16) | (uint32_t(r[4]) << 8) | r[5];
                return v4_non_public(a);
            }
            if (r[0] == 0x20 && r[1] == 0x01 && r[2] == 0 && r[3] == 0) return true;  // Teredo 2001::/32
            {   // ::/96 IPv4-compatible (deprecated)
                bool z = true; for (int i = 0; i < 12; ++i) if (r[i]) { z = false; break; }
                if (z) return true;
            }
            if (r[0] == 0x00 && r[1] == 0x64 && r[2] == 0xFF && r[3] == 0x9B && r[4] == 0x00 && r[5] == 0x01) return true; // 64:ff9b:1::/48
            return false;
        }
        return true;
    }

    bool to_raw_ipv4(uint8_t out[4]) const noexcept {
        if (m_family != AF_INET) return false;
        auto* v4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
        ::memcpy(out, &v4->sin_addr, 4); return true;
    }
    bool to_raw_ipv6(uint8_t out[16]) const noexcept {
        if (m_family != AF_INET6) return false;
        auto* v6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
        ::memcpy(out, &v6->sin6_addr, 16); return true;
    }

    knst_c16string to_string() const {
        knst_c16string r;
        if (!m_valid) return r;
        char buf[64] = {};
        if (m_family == AF_INET) {
            const void* src = &reinterpret_cast<const sockaddr_in*>(&m_addr)->sin_addr;
            if (!::inet_ntop(AF_INET, src, buf, sizeof(buf))) return knst_c16string();
        } else if (m_family == AF_INET6) {
            const void* src = &reinterpret_cast<const sockaddr_in6*>(&m_addr)->sin6_addr;
            if (!::inet_ntop(AF_INET6, src, buf, sizeof(buf))) return knst_c16string();
        } else return knst_c16string();
        uint32_t len = static_cast<uint32_t>(::strlen(buf));
        for (uint32_t i = 0; i < len; ++i) r.append(static_cast<char16_t>(buf[i]));
        return r;
    }

    const sockaddr_storage& native() const noexcept { return m_addr; }
    sockaddr_storage&       native()       noexcept { return m_addr; }

    bool operator==(const knst_ip_address& o) const noexcept {
        if (m_valid != o.m_valid || m_family != o.m_family) return false;
        if (!m_valid) return true;
        if (m_family == AF_INET) {
            return reinterpret_cast<const sockaddr_in*>(&m_addr)->sin_addr.s_addr ==
                   reinterpret_cast<const sockaddr_in*>(&o.m_addr)->sin_addr.s_addr;
        }
        return ::memcmp(
            &reinterpret_cast<const sockaddr_in6*>(&m_addr)->sin6_addr,
            &reinterpret_cast<const sockaddr_in6*>(&o.m_addr)->sin6_addr, 16) == 0;
    }
    bool operator!=(const knst_ip_address& o) const noexcept { return !(*this == o); }

private:
    static bool v4_non_public(uint32_t a) noexcept {         // host byte order
        const uint32_t o1 = a >> 24, o2 = (a >> 16) & 0xFF;
        if (o1 == 0 || o1 == 10 || o1 == 127) return true;               // this-net, RFC1918, loopback
        if (o1 == 100 && (o2 & 0xC0) == 64) return true;                 // 100.64/10 CGNAT
        if (o1 == 169 && o2 == 254) return true;                         // link-local
        if (o1 == 172 && (o2 & 0xF0) == 16) return true;                 // 172.16/12
        if (o1 == 192 && o2 == 168) return true;                         // 192.168/16
        if (o1 == 192 && o2 == 0 && ((a >> 8) & 0xFF) == 0) return true; // 192.0.0/24 IETF
                if (o1 == 192 && o2 == 0 && ((a >> 8) & 0xFF) == 2) return true; // 192.0.2/24 TEST-NET-1
        if (o1 == 192 && o2 == 88 && ((a >> 8) & 0xFF) == 99) return true; // 6to4 relay anycast
        if (o1 == 198 && (o2 == 18 || o2 == 19)) return true;            // 198.18/15 benchmark
        if (o1 == 198 && o2 == 51 && ((a >> 8) & 0xFF) == 100) return true; // TEST-NET-2
        if (o1 == 203 && o2 == 0 && ((a >> 8) & 0xFF) == 113) return true;  // TEST-NET-3
        if (o1 >= 224) return true;                                      // multicast + reserved + broadcast
        return false;
    }

    sockaddr_storage m_addr{};
    bool             m_valid  = false;
    int              m_family = 0;
};


/* ============================================================================
 * 4) knst_endpoint
 * ============================================================================ */

struct knst_endpoint {
    knst_ip_address address;
    uint16_t        port = 0;

    knst_endpoint() noexcept = default;
    knst_endpoint(const knst_ip_address& a, uint16_t p) noexcept : address(a), port(p) {}

    static knst_endpoint from_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t p) noexcept {
        return knst_endpoint(knst_ip_address::from_ipv4(a, b, c, d), p);
    }

    static knst_endpoint from_native(const sockaddr* sa) noexcept {
        knst_endpoint r;
        if (!sa) return r;
        r.address = knst_ip_address::from_raw(sa);
        if (sa->sa_family == AF_INET)
            r.port = ntohs(reinterpret_cast<const sockaddr_in*>(sa)->sin_port);
        else if (sa->sa_family == AF_INET6)
            r.port = ntohs(reinterpret_cast<const sockaddr_in6*>(sa)->sin6_port);
        return r;
    }

    sockaddr_storage to_native() const noexcept {
        sockaddr_storage s{};
        ::memcpy(&s, &address.native(), sizeof(sockaddr_storage));
        if (address.is_ipv4())
            reinterpret_cast<sockaddr_in*>(&s)->sin_port = htons(port);
        else if (address.is_ipv6())
            reinterpret_cast<sockaddr_in6*>(&s)->sin6_port = htons(port);
        return s;
    }

    socklen_t native_length() const noexcept {
        if (address.is_ipv4()) return sizeof(sockaddr_in);
        if (address.is_ipv6()) return sizeof(sockaddr_in6);
        return 0;
    }

    static knst_endpoint parse(const knst_c16string& s, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        knst_endpoint r;
        if (s.empty()) { err = knst_network_error::InvalidArgument; return r; }

                   auto parse_port = [&](uint32_t from, uint16_t& out_port) -> bool {
            if (from >= s.length()) return false;
            uint32_t port_val = 0;
            for (uint32_t i = from; i < s.length(); ++i) {
                if (s[i] < u'0' || s[i] > u'9') return false;
                port_val = port_val * 10u + static_cast<uint32_t>(s[i] - u'0');
                if (port_val > 65535u) return false;
            }
            out_port = static_cast<uint16_t>(port_val);
            return true;
        };

        if (s[0] == u'[') {
            uint32_t close = 1;
            while (close < s.length() && s[close] != u']') ++close;
            if (close >= s.length()) { err = knst_network_error::InvalidArgument; return r; }
            knst_c16string ipv6_str(s.data() + 1, close - 1);
            r.address = knst_ip_address::from_string(ipv6_str, err);
            if (err != knst_network_error::None) return r;
            if (close + 1 >= s.length()) return r;
            if (s[close + 1] != u':' || !parse_port(close + 2, r.port)) {
                err = knst_network_error::InvalidArgument; return r;
            }
            return r;
        }

        uint32_t colon_count = 0, colon = 0;
        for (uint32_t i = 0; i < s.length(); ++i)
            if (s[i] == u':') { if (colon_count == 0) colon = i; ++colon_count; }

        if (colon_count != 1) {      // bare IPv4 or bare IPv6 (no port)
            r.address = knst_ip_address::from_string(s, err);
            return r;
        }
        knst_c16string ip_str(s.data(), colon);
        r.address = knst_ip_address::from_string(ip_str, err);
        if (err != knst_network_error::None) return r;
        if (!parse_port(colon + 1, r.port)) err = knst_network_error::InvalidArgument;
        return r;
    }

    knst_c16string to_string() const {
        knst_c16string r;
        if (!address.is_valid()) return r;
        knst_c16string addr = address.to_string();
        if (address.is_ipv6()) {
            r.append(u'[');
            for (uint32_t i = 0; i < addr.length(); ++i) r.append(addr[i]);
            r.append(u']');
        } else {
            for (uint32_t i = 0; i < addr.length(); ++i) r.append(addr[i]);
        }
        r.append(u':');
        knst_net_detail::append_uint(r, port);
        return r;
    }

    bool is_valid() const noexcept { return address.is_valid() && port != 0; }
    bool operator==(const knst_endpoint& o) const noexcept {
        return address == o.address && port == o.port;
    }
    bool operator!=(const knst_endpoint& o) const noexcept { return !(*this == o); }
};


/* ============================================================================
 * 5) knst_dns_resolver — sync + async (deadline)
 * ============================================================================ */

class knst_dns_resolver {
private:
    static bool host_to_ascii(const knst_c16string& host, char* buf /*256*/,
                              bool& any_alpha, knst_network_error& err) noexcept {
        any_alpha = false;
        if (host.empty() || host.length() >= 256) { err = knst_network_error::InvalidArgument; return false; }
        uint32_t n = 0;
        for (uint32_t i = 0; i < host.length(); ++i) {
            char16_t c = host[i];
            if (c == 0 || c >= 128) { err = knst_network_error::InvalidArgument; return false; }
            char cc = static_cast<char>(c);
            if ((cc >= 'a' && cc <= 'z') || (cc >= 'A' && cc <= 'Z')) any_alpha = true;
            buf[n++] = cc;
        }
        buf[n] = '\0';
    #if defined(__ANDROID__)
        any_alpha = false;      // AI_ADDRCONFIG is unreliable on Android (no-IPv6 devices)
    #endif
        return true;
    }

    static int gai_call(const char* host, const char* port, int family,
                        bool addrconfig, struct addrinfo*& res) noexcept {
        struct addrinfo hints{};
        hints.ai_family   = family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;
        hints.ai_flags    = AI_NUMERICSERV;
        if (addrconfig) hints.ai_flags |= AI_ADDRCONFIG;
        res = nullptr;
        int rc = ::getaddrinfo(host, port, &hints, &res);
    #ifdef EAI_NONAME
        // Offline / container / loopback-only: retry without AI_ADDRCONFIG
        if (rc == EAI_NONAME && addrconfig) {
            hints.ai_flags &= ~AI_ADDRCONFIG;
            res = nullptr;
            rc = ::getaddrinfo(host, port, &hints, &res);
        }
    #endif
        return rc;
    }

    static knst_network_error map_gai(int rc) noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        switch (rc) {
            case WSAHOST_NOT_FOUND: return knst_network_error::DnsHostNotFound;
            case WSATRY_AGAIN:      return knst_network_error::DnsTemporaryFailure;
            case WSANO_DATA:        return knst_network_error::DnsNoData;
            case WSAENOBUFS:        return knst_network_error::OutOfMemory;
            default:                return knst_network_error::DnsFailure;
        }
    #else
        switch (rc) {
            #ifdef EAI_NONAME
            case EAI_NONAME:   return knst_network_error::DnsHostNotFound;
            #endif
            #ifdef EAI_AGAIN
            case EAI_AGAIN:    return knst_network_error::DnsTemporaryFailure;
            #endif
            #ifdef EAI_FAIL
            case EAI_FAIL:     return knst_network_error::DnsFailure;
            #endif
            #ifdef EAI_SYSTEM
            case EAI_SYSTEM: {
                knst_network_error e = knst_net_detail::map_errno(errno);
                return e == knst_network_error::None ? knst_network_error::DnsFailure : e;
            }
            #endif
            #ifdef EAI_MEMORY
            case EAI_MEMORY:   return knst_network_error::OutOfMemory;
            #endif
            default:           return knst_network_error::DnsFailure;
        }
    #endif
    }

    static void collect(struct addrinfo* res, uint16_t port,
                        knst_vector<knst_endpoint>& out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        for (auto* p = res; p != nullptr; p = p->ai_next) {
            knst_ip_address addr = knst_ip_address::from_raw(p->ai_addr);
            if (!addr.is_valid()) continue;
            if (!out.push_back(knst_endpoint(addr, port))) { err = knst_network_error::OutOfMemory; return; }
        }
        if (out.size() == 0) err = knst_network_error::DnsNoData;
    }

    // Number of DNS worker threads currently alive (running, incl. abandoned
    // ones that outlived their deadline). Bounded to stop thread exhaustion
    // when the resolver hangs.
    static std::atomic<uint32_t>& workers() noexcept {
        static std::atomic<uint32_t> c{0}; return c;
    }
    static constexpr uint32_t MAX_WORKERS = 32;

public:
    struct async_state {
        std::mutex              m;
        std::condition_variable cv;
        bool                    done = false;
        knst_network_error      err  = knst_network_error::None;
        knst_vector<knst_endpoint> endpoints;
        char     host[256] = {};
        char     port_s[8] = {};
        int      family = AF_UNSPEC;
        uint16_t port = 0;
        bool     addrconfig = false;
    };

    static bool resolve(const knst_c16string& host, uint16_t port,
                        knst_vector<knst_endpoint>& out, knst_network_error& err,
                        int family = AF_UNSPEC) noexcept {
        err = knst_network_error::None;
        out.clear();
        knst_net_detail::winsock_global_init();
        char host_ascii[256]; bool any_alpha = false;
        if (!host_to_ascii(host, host_ascii, any_alpha, err)) return false;
        char port_ascii[8] = {};
        ::snprintf(port_ascii, sizeof(port_ascii), "%u", static_cast<unsigned>(port));

        struct addrinfo* res = nullptr;
        int rc = gai_call(host_ascii, port_ascii, family, any_alpha, res);
        if (rc != 0) { err = map_gai(rc); return false; }
        collect(res, port, out, err);
        ::freeaddrinfo(res);
        return err == knst_network_error::None;
    }

    // Resolve with a deadline. timeout_ms == 0 => synchronous (unbounded).
    // IP literals never spawn a thread. On timeout the worker is detached and
    // finishes in the background (getaddrinfo is not cancellable); at most
    // MAX_WORKERS such threads exist at once (further lookups fail fast with
    // DnsTemporaryFailure instead of piling up threads).
    static bool resolve_async(const knst_c16string& host, uint16_t port,
                              knst_vector<knst_endpoint>& out, knst_network_error& err,
                              uint32_t timeout_ms, int family = AF_UNSPEC) noexcept {
        err = knst_network_error::None;
        out.clear();

        {   // IP literal fast path
            knst_network_error e2 = knst_network_error::None;
            knst_ip_address ip = knst_ip_address::from_string(host, e2);
            if (e2 == knst_network_error::None && ip.is_valid() &&
                (family == AF_UNSPEC || family == ip.family())) {
                if (!out.push_back(knst_endpoint(ip, port))) { err = knst_network_error::OutOfMemory; return false; }
                return true;
            }
        }
        if (timeout_ms == 0) return resolve(host, port, out, err, family);

        knst_net_detail::winsock_global_init();
        std::shared_ptr<async_state> st(new (std::nothrow) async_state());
        if (!st) { err = knst_network_error::OutOfMemory; return false; }
        bool any_alpha = false;
        if (!host_to_ascii(host, st->host, any_alpha, err)) return false;
        ::snprintf(st->port_s, sizeof(st->port_s), "%u", static_cast<unsigned>(port));
        st->family = family; st->port = port; st->addrconfig = any_alpha;

        if (workers().fetch_add(1, std::memory_order_acq_rel) >= MAX_WORKERS) {
            workers().fetch_sub(1, std::memory_order_acq_rel);
            err = knst_network_error::DnsTemporaryFailure;
            return false;
        }

        auto work = [st]() noexcept {
            struct addrinfo* res = nullptr;
            int rc = gai_call(st->host, st->port_s, st->family, st->addrconfig, res);
            knst_network_error e = knst_network_error::None;
            if (rc != 0) e = map_gai(rc);
            else { collect(res, st->port, st->endpoints, e); ::freeaddrinfo(res); }
            {
                std::lock_guard<std::mutex> lk(st->m);
                st->err = e; st->done = true;
            }
            st->cv.notify_all();
            workers().fetch_sub(1, std::memory_order_acq_rel);
        };

        std::thread th;
    #if KNST_NET_HAS_EXCEPTIONS
        try { th = std::thread(work); }
        catch (...) {                                   // no thread available: sync fallback
            workers().fetch_sub(1, std::memory_order_acq_rel);
            return resolve(host, port, out, err, family);
        }
    #else
        th = std::thread(work);
    #endif

        bool finished;
        {
            std::unique_lock<std::mutex> lk(st->m);
            finished = st->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                       [&]() { return st->done; });
        }
        if (!finished) {
            th.detach();
            err = knst_network_error::Timeout;
            return false;
        }
        th.join();
        if (st->err != knst_network_error::None) { err = st->err; return false; }
        for (uint32_t i = 0; i < st->endpoints.size(); ++i)
            if (!out.push_back(st->endpoints[i])) { err = knst_network_error::OutOfMemory; return false; }
        return true;
    }

    static bool resolve_first(const knst_c16string& host, uint16_t port,
                              knst_endpoint& out, knst_network_error& err,
                              int family = AF_UNSPEC) noexcept {
        knst_vector<knst_endpoint> list;
        if (!resolve(host, port, list, err, family)) return false;
        out = list[0];
        return true;
    }

    static bool reverse(const knst_ip_address& addr, knst_c16string& out,
                        knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();
        if (!addr.is_valid()) { err = knst_network_error::InvalidArgument; return false; }
        knst_net_detail::winsock_global_init();
        char hostbuf[NI_MAXHOST] = {};
        int rc = ::getnameinfo(reinterpret_cast<const sockaddr*>(&addr.native()),
                               addr.is_ipv4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
                               hostbuf, sizeof(hostbuf), nullptr, 0, NI_NAMEREQD);
        if (rc != 0) { err = knst_network_error::DnsFailure; return false; }
        uint32_t len = static_cast<uint32_t>(::strlen(hostbuf));
        for (uint32_t i = 0; i < len; ++i) out.append(static_cast<char16_t>(hostbuf[i]));
        return true;
    }
};


/* ============================================================================
 * 5b) knst_dns_cache — thread-safe, LRU, TTL
 * ============================================================================ */

/* ============================================================================
 * 5c) DNS wire format helpers (RFC 1035) — minimal subset used by DoH
 * ============================================================================ */

namespace knst_net_detail {

    // Encode a dotted name into DNS label format.
    // "example.com" -> 07 'e''x''a''m''p''l''e' 03 'c''o''m' 00
    inline bool dns_encode_name(const knst_c16string& name, knst_byte_string& out) noexcept {
        if (name.empty() || name.length() > 253) return false;
        uint32_t start = 0;
        const uint32_t total = name.length();
        for (uint32_t i = 0; i <= total; ++i) {
            if (i == total || name[i] == u'.') {
                uint32_t label_len = i - start;
                if (label_len == 0 || label_len > 63) return false;
                out.push_back(static_cast<uint8_t>(label_len));
                for (uint32_t k = start; k < i; ++k) {
                    char16_t c = name[k];
                    if (c == 0 || c >= 128) return false;
                    out.push_back(static_cast<uint8_t>(c));
                }
                start = i + 1;
            }
        }
        out.push_back(0);   // root label terminator
        return true;
    }

    // Build a DNS query. qtype: 1 = A, 28 = AAAA, 255 = ANY.
    inline bool dns_build_query(const knst_c16string& name,
                                uint16_t qtype,
                                uint16_t id,
                                knst_byte_string& out) noexcept {
        out.clear();
        // Header (12 bytes)
        out.push_back(static_cast<uint8_t>(id >> 8));
        out.push_back(static_cast<uint8_t>(id & 0xFF));
        out.push_back(0x01); out.push_back(0x00);   // flags: RD=1 (recursion desired)
        out.push_back(0x00); out.push_back(0x01);   // qdcount = 1
        out.push_back(0x00); out.push_back(0x00);   // ancount
        out.push_back(0x00); out.push_back(0x00);   // nscount
        out.push_back(0x00); out.push_back(0x00);   // arcount
        // Question
        if (!dns_encode_name(name, out)) return false;
        out.push_back(static_cast<uint8_t>(qtype >> 8));
        out.push_back(static_cast<uint8_t>(qtype & 0xFF));
        out.push_back(0x00); out.push_back(0x01);   // class IN
        return true;
    }

    // Advance past a (possibly compressed) QNAME.
    inline bool dns_skip_name(const uint8_t* p, size_t len, size_t& pos) noexcept {
        while (pos < len) {
            uint8_t b = p[pos];
            if (b == 0) { ++pos; return true; }
            if ((b & 0xC0) == 0xC0) { pos += 2; return pos <= len; }   // compression pointer
            if (b > 63) return false;
            pos += 1 + b;
        }
        return false;
    }

    // Parse a DNS response message. Extracts A (IPv4) and AAAA (IPv6) records
    // from the answer section. CNAMEs are skipped (the resolver must query the
    // canonical name again — DoH endpoints usually return A/AAAA alongside CNAME).
    inline bool dns_parse_response(const knst_byte_string& raw,
                                   uint16_t expected_id,
                                   knst_vector<knst_ip_address>& out,
                                   knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();
        if (raw.length() < 12) { err = knst_network_error::DnsFailure; return false; }
        const uint8_t* p = raw.data();
        const size_t len = raw.length();

        uint16_t id = (static_cast<uint16_t>(p[0]) << 8) | p[1];
        if (id != expected_id) { err = knst_network_error::DnsFailure; return false; }

        uint16_t flags = (static_cast<uint16_t>(p[2]) << 8) | p[3];
        if ((flags & 0x8000) == 0) { err = knst_network_error::DnsFailure; return false; }  // QR bit
        uint8_t rcode = static_cast<uint8_t>(flags & 0x0F);
        if (rcode == 3) { err = knst_network_error::DnsHostNotFound; return false; }
        if (rcode != 0) { err = knst_network_error::DnsFailure; return false; }

        uint16_t qdcount = (static_cast<uint16_t>(p[4]) << 8) | p[5];
        uint16_t ancount = (static_cast<uint16_t>(p[6]) << 8) | p[7];

        size_t pos = 12;
        for (uint16_t i = 0; i < qdcount; ++i) {
            if (!dns_skip_name(p, len, pos)) { err = knst_network_error::DnsFailure; return false; }
            pos += 4;   // qtype + qclass
            if (pos > len) { err = knst_network_error::DnsFailure; return false; }
        }

        for (uint16_t i = 0; i < ancount; ++i) {
            if (!dns_skip_name(p, len, pos)) { err = knst_network_error::DnsFailure; return false; }
            if (pos + 10 > len) { err = knst_network_error::DnsFailure; return false; }
            uint16_t rtype  = (static_cast<uint16_t>(p[pos])     << 8) | p[pos + 1];
            uint16_t rclass = (static_cast<uint16_t>(p[pos + 2]) << 8) | p[pos + 3];
            uint16_t rdlen  = (static_cast<uint16_t>(p[pos + 8]) << 8) | p[pos + 9];
            pos += 10;
            if (pos + rdlen > len) { err = knst_network_error::DnsFailure; return false; }

            if (rclass == 1) {   // IN class
                if (rtype == 1 && rdlen == 4) {            // A
                    knst_ip_address ip = knst_ip_address::from_ipv4(
                        p[pos], p[pos + 1], p[pos + 2], p[pos + 3]);
                    if (!out.push_back(ip)) { err = knst_network_error::OutOfMemory; return false; }
                } else if (rtype == 28 && rdlen == 16) {   // AAAA
                    uint8_t a[16];
                    ::memcpy(a, p + pos, 16);
                    knst_ip_address ip = knst_ip_address::from_ipv6(a);
                    if (!out.push_back(ip)) { err = knst_network_error::OutOfMemory; return false; }
                }
            }
            pos += rdlen;
        }

        if (out.empty()) { err = knst_network_error::DnsNoData; return false; }
        return true;
    }

} // namespace knst_net_detail





class knst_dns_cache {
public:
    static knst_dns_cache& global() noexcept { static knst_dns_cache c; return c; }

    knst_dns_cache() noexcept = default;
    ~knst_dns_cache() noexcept { clear(); }
    knst_dns_cache(const knst_dns_cache&) = delete;
    knst_dns_cache& operator=(const knst_dns_cache&) = delete;

    // Fills `out` with endpoints (port replaced). false => miss/expired.
    bool lookup(const knst_c16string& host, uint16_t port,
                knst_vector<knst_endpoint>& out) noexcept {
        char key[256];
        if (!make_key(host, key)) return false;
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(m_mutex);
        entry* prev = nullptr;
        for (entry* e = m_head; e; prev = e, e = e->next) {
            if (::strcmp(e->host, key) != 0) continue;
            if (now >= e->expiry) {
                unlink(prev, e); delete e; return false;
            }
            if (prev) { prev->next = e->next; e->next = m_head; m_head = e; }   // MRU
            out.clear();
            for (uint32_t i = 0; i < e->eps.size(); ++i) {
                knst_endpoint ep = e->eps[i];
                ep.port = port;
                if (!out.push_back(ep)) { out.clear(); return false; }
            }
            return out.size() > 0;
        }
        return false;
    }

    void store(const knst_c16string& host, const knst_vector<knst_endpoint>& eps,
               uint32_t ttl_ms) noexcept {
        if (ttl_ms == 0 || eps.size() == 0) return;
        char key[256];
        if (!make_key(host, key)) return;
        entry* ne = new (std::nothrow) entry();
        if (!ne) return;
        ::memcpy(ne->host, key, ::strlen(key) + 1);
        for (uint32_t i = 0; i < eps.size(); ++i)
            if (!ne->eps.push_back(eps[i])) { delete ne; return; }
        ne->expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(ttl_ms);

        entry* victim = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            entry* prev = nullptr;
            for (entry* e = m_head; e; prev = e, e = e->next) {
                if (::strcmp(e->host, key) == 0) { unlink(prev, e); delete e; break; }
            }
            ne->next = m_head; m_head = ne; ++m_count;
            if (m_count > m_max) {            // evict LRU (tail)
                entry* p2 = nullptr; entry* t = m_head;
                while (t && t->next) { p2 = t; t = t->next; }
                if (t && p2) { p2->next = nullptr; --m_count; victim = t; }
            }
        }
        delete victim;
    }

    void clear() noexcept {
        entry* h;
        { std::lock_guard<std::mutex> lk(m_mutex); h = m_head; m_head = nullptr; m_count = 0; }
        while (h) { entry* n = h->next; delete h; h = n; }
    }

    void set_max_entries(uint32_t n) noexcept {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_max = n == 0 ? 1 : n;
    }

private:
    struct entry {
        char host[256] = {};
        knst_vector<knst_endpoint> eps;
        std::chrono::steady_clock::time_point expiry{};
        entry* next = nullptr;
    };

    static bool make_key(const knst_c16string& host, char* key) noexcept {
        if (host.empty() || host.length() >= 256) return false;
        uint32_t n = 0;
        for (uint32_t i = 0; i < host.length(); ++i) {
            char16_t c = host[i];
            if (c == 0 || c >= 128) return false;
            if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c - u'A' + u'a');
            key[n++] = static_cast<char>(c);
        }
        key[n] = '\0';
        return true;
    }
    void unlink(entry* prev, entry* e) noexcept {
        if (prev) prev->next = e->next; else m_head = e->next;
        --m_count;
    }

    std::mutex m_mutex;
    entry*     m_head  = nullptr;
    uint32_t   m_count = 0;
    uint32_t   m_max   = 128;
};


/* ============================================================================
 * 6) knst_socket_base — common lifecycle + options
 * ============================================================================ */

class knst_socket_base {
public:
    virtual ~knst_socket_base() noexcept { knst_socket_base::close(); }

    knst_socket_base(knst_socket_base&& o) noexcept
        : m_handle(o.m_handle), m_last_error(o.m_last_error),
          m_read_timeout_ms(o.m_read_timeout_ms),
          m_write_timeout_ms(o.m_write_timeout_ms),
          m_connected(o.m_connected),
          m_local_bind(o.m_local_bind), m_has_bind(o.m_has_bind) {
        o.m_handle = KNST_INVALID_SOCKET;
        o.m_last_error = knst_network_error::None;
        o.m_connected = false;
    }

    knst_socket_base& operator=(knst_socket_base&& o) noexcept {
        if (this != &o) {
            close();
            m_handle = o.m_handle; m_last_error = o.m_last_error;
            m_read_timeout_ms = o.m_read_timeout_ms;
            m_write_timeout_ms = o.m_write_timeout_ms;
            m_connected = o.m_connected;
            m_local_bind = o.m_local_bind;
            m_has_bind   = o.m_has_bind;
            o.m_handle = KNST_INVALID_SOCKET;
            o.m_last_error = knst_network_error::None;
            o.m_connected = false;
            o.m_has_bind  = false;
        }
        return *this;
    }

    knst_socket_base(const knst_socket_base&) = delete;
    knst_socket_base& operator=(const knst_socket_base&) = delete;

    bool is_open() const noexcept { return m_handle != KNST_INVALID_SOCKET; }
    bool is_connected() const noexcept { return is_open() && m_connected; }
    knst_network_error last_error() const noexcept { return m_last_error; }
    knst_socket_handle_t native_handle() const noexcept { return m_handle; }

    virtual void close() noexcept {
        if (m_handle != KNST_INVALID_SOCKET) {
            knst_net_detail::close_handle(m_handle);
            m_handle = KNST_INVALID_SOCKET;
        }
        m_connected = false;
    }

    // Adopt a raw handle from accept() or another socket (takes ownership).
    // The handle must already be non-blocking.
    void adopt(knst_socket_handle_t h) noexcept {
        close();
        m_handle = h;
        m_connected = true;
        m_last_error = knst_network_error::None;
        if (h != KNST_INVALID_SOCKET) apply_nodelay();
    }

    knst_endpoint local_endpoint() const noexcept {
        knst_endpoint r;
        if (m_handle == KNST_INVALID_SOCKET) return r;
        sockaddr_storage ss{};
        socklen_t sl = sizeof(ss);
        if (::getsockname(m_handle, reinterpret_cast<sockaddr*>(&ss), &sl) != 0) return r;
        return knst_endpoint::from_native(reinterpret_cast<const sockaddr*>(&ss));
    }

        // ------------------------------------------------------------------
    // Source-IP selection (multi-homed hosts, VPN split routing, etc.)
    //
    // Call BEFORE connect(). The bind is applied automatically on every
    // connect attempt inside do_connect(), so connect_any() / connect_async()
    // work too. Family mismatch is skipped silently so mixed v4/v6 endpoint
    // lists fall back to default routing for the mismatched family.
    // ------------------------------------------------------------------
    bool bind_local(const knst_endpoint& local, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!local.address.is_valid()) { err = knst_network_error::InvalidArgument; return false; }
        m_local_bind = local;
        m_has_bind   = true;
        return true;
    }
    void clear_bind_local() noexcept { m_has_bind = false; }
    bool has_local_bind() const noexcept { return m_has_bind; }


    knst_endpoint remote_endpoint() const noexcept {
        knst_endpoint r;
        if (m_handle == KNST_INVALID_SOCKET) return r;
        sockaddr_storage ss{};
        socklen_t sl = sizeof(ss);
        if (::getpeername(m_handle, reinterpret_cast<sockaddr*>(&ss), &sl) != 0) return r;
        return knst_endpoint::from_native(reinterpret_cast<const sockaddr*>(&ss));
    }

    void set_read_timeout(uint32_t ms) noexcept  { m_read_timeout_ms  = ms; }
    void set_write_timeout(uint32_t ms) noexcept { m_write_timeout_ms = ms; }
    uint32_t read_timeout()  const noexcept { return m_read_timeout_ms; }
    uint32_t write_timeout() const noexcept { return m_write_timeout_ms; }

    bool set_nodelay(bool enable) noexcept {
        if (m_handle == KNST_INVALID_SOCKET) { m_last_error = knst_network_error::NotOpen; return false; }
        int v = enable ? 1 : 0;
    #ifdef TCP_NODELAY
        if (::setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY,
                         reinterpret_cast<const char*>(&v), sizeof(v)) != 0) {
            m_last_error = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            return false;
        }
        return true;
    #else
        (void)v; m_last_error = knst_network_error::NotSupported; return false;
    #endif
    }

    bool set_keepalive(bool enable) noexcept { return set_bool_opt(SOL_SOCKET, SO_KEEPALIVE, enable); }
    bool set_reuse_addr(bool enable) noexcept { return set_bool_opt(SOL_SOCKET, SO_REUSEADDR, enable); }

    void shutdown_read() noexcept {
        if (m_handle == KNST_INVALID_SOCKET) return;
    #if KNST_USING_PLATFORM_WINDOWS
        ::shutdown(m_handle, SD_RECEIVE);
    #else
        ::shutdown(m_handle, SHUT_RD);
    #endif
    }
    void shutdown_write() noexcept {
        if (m_handle == KNST_INVALID_SOCKET) return;
    #if KNST_USING_PLATFORM_WINDOWS
        ::shutdown(m_handle, SD_SEND);
    #else
        ::shutdown(m_handle, SHUT_WR);
    #endif
    }
    void shutdown_both() noexcept {
        if (m_handle == KNST_INVALID_SOCKET) return;
    #if KNST_USING_PLATFORM_WINDOWS
        ::shutdown(m_handle, SD_BOTH);
    #else
        ::shutdown(m_handle, SHUT_RDWR);
    #endif
    }

protected:
    knst_socket_base() noexcept = default;

    bool set_bool_opt(int level, int opt, bool enable) noexcept {
        if (m_handle == KNST_INVALID_SOCKET) { m_last_error = knst_network_error::NotOpen; return false; }
        int v = enable ? 1 : 0;
        if (::setsockopt(m_handle, level, opt, reinterpret_cast<const char*>(&v), sizeof(v)) != 0) {
            m_last_error = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            return false;
        }
        return true;
    }

    bool ensure_open(int family, int type, int protocol, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (m_handle != KNST_INVALID_SOCKET) return true;
        m_handle = knst_net_detail::create_socket(family, type, protocol);
        if (m_handle == KNST_INVALID_SOCKET) {
            err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            m_last_error = err; return false;
        }
        return true;
    }

    bool do_connect(const sockaddr* addr, socklen_t addr_len, uint32_t timeout_ms,
                    knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (m_handle == KNST_INVALID_SOCKET) {
            err = knst_network_error::NotOpen; m_last_error = err; return false;
        }

        // Apply pending local bind (source-IP selection).
        if (m_has_bind && m_local_bind.address.is_valid()) {
            const int conn_family = addr->sa_family;
            const int loc_family  = m_local_bind.address.is_ipv4() ? AF_INET
                                  : m_local_bind.address.is_ipv6() ? AF_INET6 : 0;
            if (loc_family == conn_family) {
                sockaddr_storage lss = m_local_bind.to_native();
                if (::bind(m_handle, reinterpret_cast<const sockaddr*>(&lss),
                           m_local_bind.native_length()) != 0) {
                    err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
                    m_last_error = err;
                    close();
                    return false;
                }
            }
            // Family mismatch: skip silently (default routing for that family).
        }

        int rc = ::connect(m_handle, addr, addr_len);
        if (rc == 0) {
            m_connected = true;
            apply_nodelay();
            return true;
        }
        int se = knst_net_detail::last_socket_error();
    #if KNST_USING_PLATFORM_WINDOWS
        bool in_progress = (se == WSAEWOULDBLOCK || se == WSAEINPROGRESS || se == WSAEINTR);
    #else
        bool in_progress = (se == EINPROGRESS || se == EINTR);
    #endif
        if (!in_progress) {
            err = knst_net_detail::map_errno(se);
            m_last_error = err;
            close();
            return false;
        }
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        if (!knst_net_detail::poll_wait(m_handle, false, dl, err)) {
            m_last_error = err; close(); return false;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        ::getsockopt(m_handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &sl);
        if (soerr != 0) {
            err = knst_net_detail::map_errno(soerr);
            m_last_error = err; close(); return false;
        }
        m_connected = true;
        apply_nodelay();
        return true;
    }

    void apply_nodelay() noexcept {
    #ifdef TCP_NODELAY
        int fam = 0;
        sockaddr_storage ss{}; socklen_t sl = sizeof(ss);
        if (::getsockname(m_handle, reinterpret_cast<sockaddr*>(&ss), &sl) == 0) fam = ss.ss_family;
        if (fam == AF_INET || fam == AF_INET6) {      // not for AF_UNIX
            int v = 1;
            ::setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&v), sizeof(v));
        }
    #endif
    }

    knst_socket_handle_t m_handle = KNST_INVALID_SOCKET;
    knst_network_error   m_last_error = knst_network_error::None;
    uint32_t m_read_timeout_ms  = 0;
    uint32_t m_write_timeout_ms = 0;
    bool     m_connected        = false;
    knst_endpoint m_local_bind;          // pending source bind (empty = default routing)
    bool     m_has_bind         = false;
};


/* ============================================================================
 * 7) knst_stream_socket_base — deadline-based stream I/O (virtual: TLS-safe)
 * ============================================================================ */

class knst_stream_socket_base : public knst_socket_base {
public:
    knst_stream_socket_base() noexcept = default;

private:
    static constexpr size_t MAX_IO = size_t(1) << 30;

    long raw_send(const void* p, size_t len, int& se) noexcept {
        if (len > MAX_IO) len = MAX_IO;
    #if KNST_USING_PLATFORM_WINDOWS
        int n = ::send(m_handle, static_cast<const char*>(p), static_cast<int>(len), 0);
        if (n == SOCKET_ERROR) { se = ::WSAGetLastError(); return -1; }
        return n;
    #else
        ssize_t n = ::send(m_handle, p, len, KNST_MSG_NOSIGNAL);
        if (n < 0) { se = errno; return -1; }
        return static_cast<long>(n);
    #endif
    }

    long raw_recv(void* p, size_t cap, int& se) noexcept {
        if (cap > MAX_IO) cap = MAX_IO;
    #if KNST_USING_PLATFORM_WINDOWS
        int n = ::recv(m_handle, static_cast<char*>(p), static_cast<int>(cap), 0);
        if (n == SOCKET_ERROR) { se = ::WSAGetLastError(); return -1; }
        return n;
    #else
        ssize_t n = ::recv(m_handle, p, cap, 0);
        if (n < 0) { se = errno; return -1; }
        return static_cast<long>(n);
    #endif
    }

public:
    // true while already-received bytes are buffered above the raw socket (TLS).
    virtual bool has_pending_input() const noexcept { return false; }

    // One write attempt (>=1 byte on success).
    virtual bool send(const void* data, size_t len, size_t& sent, uint32_t timeout_ms,
                      knst_network_error& err) noexcept {
        sent = 0; err = knst_network_error::None;
        if (m_handle == KNST_INVALID_SOCKET) { err = knst_network_error::NotOpen; m_last_error = err; return false; }
        if (!data && len) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        if (len == 0) return true;
        knst_net_detail::deadline dl;          // created lazily: fast path costs no clock read
        bool have_dl = false;
        for (;;) {
            int se = 0;
            long n = raw_send(data, len, se);
            if (n > 0) { sent = static_cast<size_t>(n); return true; }
            if (n == 0) { err = knst_network_error::Unknown; m_last_error = err; return false; }
            knst_network_error e = knst_net_detail::map_errno(se);
            if (e == knst_network_error::Interrupted) continue;
            if (e != knst_network_error::WouldBlock) { err = e; m_last_error = e; return false; }
            if (!have_dl) {
                dl = knst_net_detail::deadline::after(knst_net_detail::resolve_timeout(timeout_ms, m_write_timeout_ms));
                have_dl = true;
            }
            if (!knst_net_detail::poll_wait(m_handle, false, dl, err)) { m_last_error = err; return false; }
        }
    }

    virtual bool send_all(const void* data, size_t len, uint32_t timeout_ms,
                          knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!data && len) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        if (len == 0) return true;
        auto p = static_cast<const uint8_t*>(data);
        auto dl = knst_net_detail::deadline::after(
            knst_net_detail::resolve_timeout(timeout_ms, m_write_timeout_ms));
        while (len > 0) {
            bool expired; uint32_t rem = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; m_last_error = err; return false; }
            size_t n = 0;
            if (!this->send(p, len, n, rem, err)) return false;    // virtual
            p += n; len -= n;
        }
        return true;
    }
    bool send_all(const knst_byte_string& data, uint32_t timeout_ms, knst_network_error& err) noexcept {
        return this->send_all(data.data(), data.length(), timeout_ms, err);   // virtual
    }

    // true + got == 0  => clean EOF
    virtual bool recv(void* buf, size_t cap, size_t& got, uint32_t timeout_ms,
                      knst_network_error& err) noexcept {
        got = 0; err = knst_network_error::None;
        if (m_handle == KNST_INVALID_SOCKET) { err = knst_network_error::NotOpen; m_last_error = err; return false; }
        if (!buf || cap == 0) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        knst_net_detail::deadline dl;
        bool have_dl = false;
        for (;;) {
            int se = 0;
            long n = raw_recv(buf, cap, se);
            if (n >= 0) { got = static_cast<size_t>(n); return true; }
            knst_network_error e = knst_net_detail::map_errno(se);
            if (e == knst_network_error::Interrupted) continue;
            if (e != knst_network_error::WouldBlock) { err = e; m_last_error = e; return false; }
            if (!have_dl) {
                dl = knst_net_detail::deadline::after(knst_net_detail::resolve_timeout(timeout_ms, m_read_timeout_ms));
                have_dl = true;
            }
            if (!knst_net_detail::poll_wait(m_handle, true, dl, err)) { m_last_error = err; return false; }
        }
    }

    bool recv(knst_byte_string& out, uint32_t timeout_ms, knst_network_error& err,
              size_t max_bytes = 4096) noexcept {
        out.clear();
        out.resize(static_cast<uint32_t>(max_bytes));
        if (out.length() < max_bytes) { err = knst_network_error::OutOfMemory; return false; }
        size_t n = 0;
        if (!this->recv(&out[0], max_bytes, n, timeout_ms, err)) { out.clear(); return false; }
        out.resize(static_cast<uint32_t>(n));
        return true;
    }

    virtual bool recv_exact(void* buf, size_t n, uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        uint8_t* p = static_cast<uint8_t*>(buf);
        size_t left = n;
        auto dl = knst_net_detail::deadline::after(
            knst_net_detail::resolve_timeout(timeout_ms, m_read_timeout_ms));
        while (left > 0) {
            bool expired; uint32_t rem = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; m_last_error = err; return false; }
            size_t got = 0;
            if (!this->recv(p, left, got, rem, err)) { m_last_error = err; return false; }   // virtual
            if (got == 0) { err = knst_network_error::ConnectionClosed; m_last_error = err; return false; }
            p += got; left -= got;
        }
        return true;
    }
};


/* ============================================================================
 * 8) knst_tcp_socket — client
 * ============================================================================ */

class knst_tcp_socket : public knst_stream_socket_base {
public:
    knst_tcp_socket() noexcept = default;

    bool connect(const knst_endpoint& ep, uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        close();
        if (!ep.is_valid()) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        int family = ep.address.is_ipv4() ? AF_INET : AF_INET6;
        if (!ensure_open(family, SOCK_STREAM, 0, err)) return false;
        sockaddr_storage native = ep.to_native();
        socklen_t native_len = ep.native_length();
        return do_connect(reinterpret_cast<const sockaddr*>(&native), native_len, timeout_ms, err);
    }

    // Tries each endpoint in order within one overall deadline (0 = infinite).
    bool connect_any(const knst_vector<knst_endpoint>& list, uint32_t timeout_ms,
                     knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (list.size() == 0) { err = knst_network_error::DnsNoData; return false; }
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        for (uint32_t i = 0; i < list.size(); ++i) {
            bool expired; uint32_t rem = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; return false; }
            uint32_t budget = knst_net_detail::attempt_budget(rem, list.size() - i);
            if (connect(list[i], budget, err)) return true;
        }
        return false;
    }

    // Synchronous DNS (no DNS timeout!). Prefer connect_async().
    bool connect(const knst_c16string& host, uint16_t port, uint32_t timeout_ms,
                 knst_network_error& err) noexcept {
        err = knst_network_error::None;
        knst_vector<knst_endpoint> list;
        if (!knst_dns_resolver::resolve(host, port, list, err)) return false;
        return connect_any(list, timeout_ms, err);
    }

    // Async DNS + connect inside ONE overall deadline (connect_timeout_ms,
    // 0 = infinite). dns_timeout_ms caps the DNS part (0 = use what is left).
    bool connect_async(const knst_c16string& host, uint16_t port,
                       uint32_t dns_timeout_ms, uint32_t connect_timeout_ms,
                       knst_network_error& err) noexcept {
        err = knst_network_error::None;
        auto dl = knst_net_detail::deadline::after(
            connect_timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : connect_timeout_ms);
        knst_vector<knst_endpoint> list;
        if (!knst_dns_resolver::resolve_async(host, port, list, err,
                knst_net_detail::dns_budget(dns_timeout_ms, dl)))
            return false;
        bool expired; uint32_t rem = dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; return false; }
        return connect_any(list, rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem, err);
    }
};


/* ============================================================================
 * 9) knst_tcp_server
 * ============================================================================ */

class knst_tcp_server {
public:
    knst_tcp_server() noexcept = default;
    ~knst_tcp_server() noexcept { close(); }
    knst_tcp_server(const knst_tcp_server&) = delete;
    knst_tcp_server& operator=(const knst_tcp_server&) = delete;

    knst_tcp_server(knst_tcp_server&& o) noexcept
        : m_handle(o.m_handle), m_last_error(o.m_last_error) {
        o.m_handle = KNST_INVALID_SOCKET;
        o.m_last_error = knst_network_error::None;
    }

    knst_tcp_server& operator=(knst_tcp_server&& o) noexcept {
        if (this != &o) {
            close();
            m_handle = o.m_handle; m_last_error = o.m_last_error;
            o.m_handle = KNST_INVALID_SOCKET;
            o.m_last_error = knst_network_error::None;
        }
        return *this;
    }

    bool is_open() const noexcept { return m_handle != KNST_INVALID_SOCKET; }
    knst_network_error last_error() const noexcept { return m_last_error; }
    knst_socket_handle_t native_handle() const noexcept { return m_handle; }

    knst_endpoint local_endpoint() const noexcept {
        knst_endpoint r;
        if (m_handle == KNST_INVALID_SOCKET) return r;
        sockaddr_storage ss{};
        socklen_t sl = sizeof(ss);
        if (::getsockname(m_handle, reinterpret_cast<sockaddr*>(&ss), &sl) != 0) return r;
        return knst_endpoint::from_native(reinterpret_cast<const sockaddr*>(&ss));
    }

    bool listen(const knst_endpoint& local, int backlog, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        close();
        if (!local.address.is_valid()) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        int family = local.address.is_ipv4() ? AF_INET : AF_INET6;
        m_handle = knst_net_detail::create_socket(family, SOCK_STREAM, 0);
        if (m_handle == KNST_INVALID_SOCKET) {
            err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            m_last_error = err; return false;
        }
        int one = 1;
    #if KNST_USING_PLATFORM_WINDOWS
        ::setsockopt(m_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&one), sizeof(one));
    #else
        ::setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
    #endif
        sockaddr_storage native = local.to_native();
        socklen_t native_len = local.native_length();
        if (::bind(m_handle, reinterpret_cast<const sockaddr*>(&native), native_len) != 0) {
            err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            m_last_error = err; close(); return false;
        }
        if (::listen(m_handle, backlog > 0 ? backlog : 128) != 0) {
            err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            m_last_error = err; close(); return false;
        }
        return true;
    }

    bool accept(knst_tcp_socket& out, uint32_t timeout_ms, knst_network_error& err) noexcept {
        knst_endpoint peer;
        return accept(out, peer, timeout_ms, err);
    }

    bool accept(knst_tcp_socket& out, knst_endpoint& peer, uint32_t timeout_ms,
                knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (m_handle == KNST_INVALID_SOCKET) { err = knst_network_error::NotOpen; m_last_error = err; return false; }
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        for (;;) {
            if (!knst_net_detail::poll_wait(m_handle, true, dl, err)) {
                m_last_error = err; return false;
            }
            sockaddr_storage peer_ss{};
            socklen_t peer_len = sizeof(peer_ss);
            knst_socket_handle_t conn = ::accept(m_handle,
                reinterpret_cast<sockaddr*>(&peer_ss), &peer_len);
            if (conn == KNST_INVALID_SOCKET) {
                knst_network_error e = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
                if (e == knst_network_error::WouldBlock ||
                    e == knst_network_error::Interrupted ||
                    e == knst_network_error::ConnectionAborted) continue;
                err = e; m_last_error = e; return false;
            }
            knst_net_detail::set_blocking(conn, false);
            knst_net_detail::set_no_inherit(conn);
        #if !KNST_USING_PLATFORM_WINDOWS
            #ifdef SO_NOSIGPIPE
            { int one = 1; ::setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)); }
            #endif
        #endif
            out.adopt(conn);                      // also enables TCP_NODELAY
            peer = knst_endpoint::from_native(reinterpret_cast<const sockaddr*>(&peer_ss));
            return true;
        }
    }

    void close() noexcept {
        if (m_handle != KNST_INVALID_SOCKET) {
            knst_net_detail::close_handle(m_handle);
            m_handle = KNST_INVALID_SOCKET;
        }
    }

private:
    knst_socket_handle_t m_handle = KNST_INVALID_SOCKET;
    knst_network_error   m_last_error = knst_network_error::None;
};


/* ============================================================================
 * 10) knst_udp_socket
 * ============================================================================ */

class knst_udp_socket : public knst_socket_base {
public:
    knst_udp_socket() noexcept = default;

    bool bind(const knst_endpoint& local, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!local.address.is_valid()) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        int family = local.address.is_ipv4() ? AF_INET : AF_INET6;
        if (!ensure_open(family, SOCK_DGRAM, 0, err)) return false;
        int one = 1;
    #if !KNST_USING_PLATFORM_WINDOWS
        ::setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
    #else
        ::setsockopt(m_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&one), sizeof(one));
    #endif
        sockaddr_storage native = local.to_native();
        socklen_t native_len = local.native_length();
        if (::bind(m_handle, reinterpret_cast<const sockaddr*>(&native), native_len) != 0) {
            err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            m_last_error = err; return false;
        }
    #if KNST_USING_PLATFORM_WINDOWS
        #ifndef SIO_UDP_CONNRESET
            #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
        #endif
        DWORD bytes = 0; BOOL off = FALSE;
        ::WSAIoctl(m_handle, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
    #endif
        m_family = family;
        return true;
    }

    bool send_to(const void* data, size_t len, const knst_endpoint& dest,
                 size_t& bytes_sent, uint32_t timeout_ms, knst_network_error& err) noexcept {
        bytes_sent = 0; err = knst_network_error::None;
        if (!dest.address.is_valid()) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        if (!data && len) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        if (m_handle == KNST_INVALID_SOCKET) {
            int family = dest.address.is_ipv4() ? AF_INET : AF_INET6;
            if (!ensure_open(family, SOCK_DGRAM, 0, err)) return false;
            m_family = family;
        }
        sockaddr_storage native = dest.to_native();
        socklen_t native_len = dest.native_length();
        auto dl = knst_net_detail::deadline::after(
            knst_net_detail::resolve_timeout(timeout_ms, m_write_timeout_ms));
        for (;;) {
            if (!knst_net_detail::poll_wait(m_handle, false, dl, err)) { m_last_error = err; return false; }
        #if KNST_USING_PLATFORM_WINDOWS
            int n = ::sendto(m_handle, reinterpret_cast<const char*>(data),
                             static_cast<int>(len), 0,
                             reinterpret_cast<const sockaddr*>(&native), native_len);
            if (n >= 0) { bytes_sent = static_cast<size_t>(n); return true; }
        #else
            ssize_t n = ::sendto(m_handle, data, len, KNST_MSG_NOSIGNAL,
                                 reinterpret_cast<const sockaddr*>(&native), native_len);
            if (n >= 0) { bytes_sent = static_cast<size_t>(n); return true; }
        #endif
            knst_network_error e = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            if (e == knst_network_error::Interrupted) continue;
            if (e != knst_network_error::WouldBlock) { err = e; m_last_error = e; return false; }
        }
    }

    bool recv_from(void* buf, size_t cap, size_t& bytes_read, knst_endpoint& from,
                   uint32_t timeout_ms, knst_network_error& err) noexcept {
        bytes_read = 0; err = knst_network_error::None;
        if (m_handle == KNST_INVALID_SOCKET) { err = knst_network_error::NotOpen; m_last_error = err; return false; }
        if (!buf || cap == 0) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        auto dl = knst_net_detail::deadline::after(
            knst_net_detail::resolve_timeout(timeout_ms, m_read_timeout_ms));
        for (;;) {
            if (!knst_net_detail::poll_wait(m_handle, true, dl, err)) { m_last_error = err; return false; }
            sockaddr_storage from_ss{};
            socklen_t from_len = sizeof(from_ss);
        #if KNST_USING_PLATFORM_WINDOWS
            int n = ::recvfrom(m_handle, reinterpret_cast<char*>(buf), static_cast<int>(cap), 0,
                               reinterpret_cast<sockaddr*>(&from_ss), &from_len);
        #else
            ssize_t n = ::recvfrom(m_handle, buf, cap, 0,
                                   reinterpret_cast<sockaddr*>(&from_ss), &from_len);
        #endif
            if (n >= 0) {
                bytes_read = static_cast<size_t>(n);
                from = knst_endpoint::from_native(reinterpret_cast<const sockaddr*>(&from_ss));
                return true;
            }
            knst_network_error e = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            if (e == knst_network_error::Interrupted || e == knst_network_error::WouldBlock) continue;
            if (e == knst_network_error::ConnectionReset) continue;   // Windows ICMP port-unreachable
            err = e; m_last_error = e; return false;
        }
    }

    bool recv_from(knst_byte_string& out, knst_endpoint& from, uint32_t timeout_ms,
                   knst_network_error& err, size_t max_bytes = 65536) noexcept {
        out.clear();
        out.resize(static_cast<uint32_t>(max_bytes));
        if (out.length() < max_bytes) { err = knst_network_error::OutOfMemory; return false; }
        size_t n = 0;
        if (!recv_from(&out[0], max_bytes, n, from, timeout_ms, err)) { out.clear(); return false; }
        out.resize(static_cast<uint32_t>(n));
        return true;
    }

    bool set_broadcast(bool enable) noexcept { return set_bool_opt(SOL_SOCKET, SO_BROADCAST, enable); }

    bool set_multicast_ttl(int ttl) noexcept {
        if (m_handle == KNST_INVALID_SOCKET) { m_last_error = knst_network_error::NotOpen; return false; }
        int v = ttl;
        int level, opt;
        if (m_family == AF_INET6) {
        #ifdef IPV6_MULTICAST_HOPS
            level = IPPROTO_IPV6; opt = IPV6_MULTICAST_HOPS;
        #else
            m_last_error = knst_network_error::NotSupported; return false;
        #endif
        } else {
        #ifdef IP_MULTICAST_TTL
            level = IPPROTO_IP; opt = IP_MULTICAST_TTL;
        #else
            m_last_error = knst_network_error::NotSupported; return false;
        #endif
        }
        if (::setsockopt(m_handle, level, opt, reinterpret_cast<const char*>(&v), sizeof(v)) != 0) {
            m_last_error = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            return false;
        }
        return true;
    }

    bool join_multicast(const knst_ip_address& group,
                        const knst_ip_address& iface = knst_ip_address{}) noexcept {
        return multicast_op(group, iface, true);
    }
    bool leave_multicast(const knst_ip_address& group,
                         const knst_ip_address& iface = knst_ip_address{}) noexcept {
        return multicast_op(group, iface, false);
    }

private:
    bool multicast_op(const knst_ip_address& group, const knst_ip_address& iface, bool join) noexcept {
        if (m_handle == KNST_INVALID_SOCKET) { m_last_error = knst_network_error::NotOpen; return false; }
        if (!group.is_multicast()) { m_last_error = knst_network_error::InvalidArgument; return false; }
        if (group.is_ipv4()) {
        #if defined(IP_ADD_MEMBERSHIP) && defined(IP_DROP_MEMBERSHIP)
            struct ip_mreq mreq{};
            ::memcpy(&mreq.imr_multiaddr,
                     &reinterpret_cast<const sockaddr_in*>(&group.native())->sin_addr, sizeof(in_addr));
            if (iface.is_ipv4())
                ::memcpy(&mreq.imr_interface,
                         &reinterpret_cast<const sockaddr_in*>(&iface.native())->sin_addr, sizeof(in_addr));
            else
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            if (::setsockopt(m_handle, IPPROTO_IP, join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP,
                             reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0) {
                m_last_error = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
                return false;
            }
            return true;
        #else
            m_last_error = knst_network_error::NotSupported; return false;
        #endif
        }
        if (group.is_ipv6()) {
        #if defined(IPV6_ADD_MEMBERSHIP) && defined(IPV6_DROP_MEMBERSHIP)
            struct ipv6_mreq mreq{};
            ::memcpy(&mreq.ipv6mr_multiaddr,
                     &reinterpret_cast<const sockaddr_in6*>(&group.native())->sin6_addr, sizeof(in6_addr));
            mreq.ipv6mr_interface = 0;
            if (::setsockopt(m_handle, IPPROTO_IPV6, join ? IPV6_ADD_MEMBERSHIP : IPV6_DROP_MEMBERSHIP,
                             reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0) {
                m_last_error = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
                return false;
            }
            return true;
        #else
            m_last_error = knst_network_error::NotSupported; return false;
        #endif
        }
        m_last_error = knst_network_error::InvalidArgument;
        return false;
    }

    int m_family = AF_INET;
};


/* ============================================================================
 * 11) Unix domain sockets
 * ============================================================================ */

#if !KNST_USING_PLATFORM_WINDOWS || KNST_NETWORK_HAS_AF_UNIX_WIN

class knst_unix_socket : public knst_stream_socket_base {
public:
    knst_unix_socket() noexcept = default;

    bool connect(const knst_c16string& path, uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        close();
        if (path.empty()) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        knst_byte_string u8 = knst_net_detail::to_utf8(path);
        if (u8.length() == 0 || u8.length() >= sizeof(sockaddr_un::sun_path)) {
            err = knst_network_error::InvalidArgument; m_last_error = err; return false;
        }
        if (!ensure_open(AF_UNIX, SOCK_STREAM, 0, err)) return false;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        for (uint32_t i = 0; i < u8.length(); ++i) addr.sun_path[i] = static_cast<char>(u8[i]);
        addr.sun_path[u8.length()] = '\0';
        return do_connect(reinterpret_cast<const sockaddr*>(&addr), sizeof(addr), timeout_ms, err);
    }
};

class knst_unix_server {
public:
    knst_unix_server() noexcept = default;
    ~knst_unix_server() noexcept { close(); }
    knst_unix_server(const knst_unix_server&) = delete;
    knst_unix_server& operator=(const knst_unix_server&) = delete;

    bool is_open() const noexcept { return m_handle != KNST_INVALID_SOCKET; }
    knst_network_error last_error() const noexcept { return m_last_error; }

    bool listen(const knst_c16string& path, int backlog, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        close();
        if (path.empty()) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        knst_byte_string u8 = knst_net_detail::to_utf8(path);
        if (u8.length() == 0 || u8.length() >= sizeof(sockaddr_un::sun_path)) {
            err = knst_network_error::InvalidArgument; m_last_error = err; return false;
        }
        m_handle = knst_net_detail::create_socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_handle == KNST_INVALID_SOCKET) {
            err = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
            m_last_error = err; return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        for (uint32_t i = 0; i < u8.length(); ++i) addr.sun_path[i] = static_cast<char>(u8[i]);
        addr.sun_path[u8.length()] = '\0';

    #if !KNST_USING_PLATFORM_WINDOWS
        struct stat st{};
        if (::lstat(addr.sun_path, &st) == 0) {
            if (!S_ISSOCK(st.st_mode)) {
                err = knst_network_error::AddressInUse; m_last_error = err; close(); return false;
            }
            int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (probe >= 0) {
                ::fcntl(probe, F_SETFD, FD_CLOEXEC);
                int fl = ::fcntl(probe, F_GETFL, 0);
                if (fl >= 0) ::fcntl(probe, F_SETFL, fl | O_NONBLOCK);
                sockaddr_un paddr = addr;
                int c = ::connect(probe, reinterpret_cast<sockaddr*>(&paddr), sizeof(paddr));
                int pe = errno;
                ::close(probe);
                if (c == 0 || pe == EINPROGRESS || pe == EAGAIN) {   // alive server
                    err = knst_network_error::AddressInUse; m_last_error = err; close(); return false;
                }
                ::unlink(addr.sun_path);                              // stale socket file
            }
        }
        mode_t old_umask = ::umask(0177);
    #else
        wchar_t wpath[520] = {};
        int wlen = ::MultiByteToWideChar(CP_UTF8, 0, addr.sun_path, -1, wpath, 520);
        if (wlen > 0) {
            DWORD attrs = ::GetFileAttributesW(wpath);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                ::DeleteFileW(wpath);
        }
    #endif

        bool ok = (::bind(m_handle, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) &&
                  (::listen(m_handle, backlog > 0 ? backlog : 128) == 0);
        int se = ok ? 0 : knst_net_detail::last_socket_error();
    #if !KNST_USING_PLATFORM_WINDOWS
        ::umask(old_umask);                 // restored on every path
    #endif
        if (!ok) {
            err = knst_net_detail::map_errno(se);
            m_last_error = err;
            knst_net_detail::close_handle(m_handle);
            m_handle = KNST_INVALID_SOCKET;
            return false;
        }
        m_bound_path = path;
        return true;
    }

    bool accept(knst_unix_socket& out, uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (m_handle == KNST_INVALID_SOCKET) { err = knst_network_error::NotOpen; m_last_error = err; return false; }
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        for (;;) {
            if (!knst_net_detail::poll_wait(m_handle, true, dl, err)) { m_last_error = err; return false; }
            knst_socket_handle_t conn = ::accept(m_handle, nullptr, nullptr);
            if (conn == KNST_INVALID_SOCKET) {
                knst_network_error e = knst_net_detail::map_errno(knst_net_detail::last_socket_error());
                if (e == knst_network_error::WouldBlock ||
                    e == knst_network_error::Interrupted ||
                    e == knst_network_error::ConnectionAborted) continue;
                err = e; m_last_error = e; return false;
            }
            knst_net_detail::set_blocking(conn, false);
            knst_net_detail::set_no_inherit(conn);
            out.adopt(conn);
            return true;
        }
    }

    void close() noexcept {
        if (m_handle != KNST_INVALID_SOCKET) {
            knst_net_detail::close_handle(m_handle);
            m_handle = KNST_INVALID_SOCKET;
        }
    #if !KNST_USING_PLATFORM_WINDOWS
        if (!m_bound_path.empty()) {
            knst_byte_string u8 = knst_net_detail::to_utf8(m_bound_path);
            if (u8.length() > 0 && u8.length() < sizeof(sockaddr_un::sun_path)) {
                char buf[sizeof(sockaddr_un::sun_path)] = {};
                for (uint32_t i = 0; i < u8.length(); ++i) buf[i] = static_cast<char>(u8[i]);
                struct stat st{};
                if (::lstat(buf, &st) == 0 && S_ISSOCK(st.st_mode)) ::unlink(buf);
            }
        }
    #endif
        m_bound_path.clear();
    }

private:
    knst_socket_handle_t m_handle = KNST_INVALID_SOCKET;
    knst_network_error   m_last_error = knst_network_error::None;
    knst_c16string       m_bound_path;
};

#endif // AF_UNIX supported


/* ============================================================================
 * 12) knst_url
 * ============================================================================ */

class knst_url {
public:
    knst_c16string scheme, userinfo, host, path, query, fragment;
    uint16_t       port = 0;

    knst_url() noexcept = default;
    bool is_valid() const noexcept { return !scheme.empty() && !host.empty(); }

    uint16_t effective_port() const noexcept {
        if (port != 0) return port;
        if (scheme == u"https") return 443;
        if (scheme == u"http")  return 80;
        return 0;
    }

private:
    // Port digits in [from,to). Port 0 is rejected (cannot be connected to).
    static bool parse_port(const knst_c16string& s, uint32_t from, uint32_t to, uint16_t& out) noexcept {
        if (from >= to) return false;
        uint32_t p = 0;
        for (uint32_t k = from; k < to; ++k) {
            if (s[k] < u'0' || s[k] > u'9') return false;
            p = p * 10u + static_cast<uint32_t>(s[k] - u'0');
            if (p > 65535u) return false;
        }
        if (p == 0) return false;
        out = static_cast<uint16_t>(p);
        return true;
    }
    static bool valid_host_char(char16_t c) noexcept {
        if (c <= 0x20 || c >= 0x7F) return false;
        switch (c) {
            case u'/': case u'\\': case u'?': case u'#': case u'@':
            case u'<': case u'>': case u'"': case u'^': case u'`':
            case u'{': case u'|': case u'}': return false;
            default: return true;
        }
    }

public:
    static bool parse(const knst_c16string& s, knst_url& out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out = knst_url{};
        if (s.empty()) { err = knst_network_error::InvalidArgument; return false; }

        uint32_t i = 0;
        while (i < s.length() && s[i] != u':') ++i;
        if (i == 0 || i >= s.length() || i + 2 >= s.length() ||
            s[i + 1] != u'/' || s[i + 2] != u'/') {
            err = knst_network_error::HttpParseUrl; return false;
        }
        out.scheme = s.substr(0, i);
        for (uint32_t k = 0; k < out.scheme.length(); ++k) {
            char16_t c = out.scheme[k];
            bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                      (k > 0 && ((c >= u'0' && c <= u'9') || c == u'+' || c == u'-' || c == u'.'));
            if (!ok) { err = knst_network_error::HttpParseUrl; return false; }
            if (c >= u'A' && c <= u'Z') out.scheme[k] = static_cast<char16_t>(c - u'A' + u'a');
        }
        i += 3;

        const uint32_t NPOS = static_cast<uint32_t>(-1);
        uint32_t frag_pos = NPOS;
        for (uint32_t k = i; k < s.length(); ++k) if (s[k] == u'#') { frag_pos = k; break; }
        uint32_t header_end = frag_pos != NPOS ? frag_pos : s.length();
        if (frag_pos != NPOS) out.fragment = s.substr(frag_pos + 1, s.length() - frag_pos - 1);

        uint32_t query_pos = NPOS;
        for (uint32_t k = i; k < header_end; ++k) if (s[k] == u'?') { query_pos = k; break; }
        uint32_t authority_end = query_pos != NPOS ? query_pos : header_end;
        if (query_pos != NPOS) out.query = s.substr(query_pos + 1, header_end - query_pos - 1);

        uint32_t path_pos = NPOS;
        for (uint32_t k = i; k < authority_end; ++k) if (s[k] == u'/') { path_pos = k; break; }
        uint32_t authority_stop = path_pos != NPOS ? path_pos : authority_end;
        out.path = path_pos != NPOS ? s.substr(path_pos, authority_end - path_pos)
                                    : knst_c16string(u"/");

        uint32_t at = NPOS;
        for (uint32_t k = i; k < authority_stop; ++k) if (s[k] == u'@') at = k;
        uint32_t host_start = i;
        if (at != NPOS) { out.userinfo = s.substr(i, at - i); host_start = at + 1; }

        if (host_start < authority_stop && s[host_start] == u'[') {
            uint32_t close = host_start + 1;
            while (close < authority_stop && s[close] != u']') ++close;
            if (close >= authority_stop) { err = knst_network_error::HttpParseUrl; return false; }
            out.host = s.substr(host_start + 1, close - host_start - 1);
            for (uint32_t k = 0; k < out.host.length(); ++k) {
                char16_t c = out.host[k];
                bool ok = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') ||
                          (c >= u'A' && c <= u'F') || c == u':' || c == u'.' || c == u'%';
                if (!ok) { err = knst_network_error::HttpParseUrl; return false; }
            }
            if (close + 1 < authority_stop) {
                if (s[close + 1] != u':') { err = knst_network_error::HttpParseUrl; return false; }
                if (close + 2 < authority_stop &&                       // "[::1]:" = default port
                    !parse_port(s, close + 2, authority_stop, out.port)) {
                    err = knst_network_error::HttpParseUrl; return false;
                }
            }
        } else {
            uint32_t colon = NPOS;
            for (uint32_t k = host_start; k < authority_stop; ++k) if (s[k] == u':') { colon = k; break; }
            uint32_t host_end = colon != NPOS ? colon : authority_stop;
            out.host = s.substr(host_start, host_end - host_start);
            for (uint32_t k = 0; k < out.host.length(); ++k)
                if (!valid_host_char(out.host[k])) { err = knst_network_error::HttpParseUrl; return false; }
            if (colon != NPOS && colon + 1 < authority_stop &&          // "host:" = default port
                !parse_port(s, colon + 1, authority_stop, out.port)) {
                err = knst_network_error::HttpParseUrl; return false;
            }
        }

        if (out.scheme.empty() || out.host.empty()) { err = knst_network_error::HttpParseUrl; return false; }
        out.host = knst_net_detail::lower16(out.host);
        return true;
    }

    knst_c16string to_string(bool include_fragment = true) const {
        knst_c16string r;
        if (!is_valid()) return r;
        r.append(scheme); r.append(u"://");
        if (!userinfo.empty()) { r.append(userinfo); r.append(u'@'); }
        r.append(host_bracketed());
        if (port != 0) { r.append(u':'); knst_net_detail::append_uint(r, port); }
        if (!path.empty()) r.append(path);
        if (!query.empty())    { r.append(u'?'); r.append(query); }
        if (include_fragment && !fragment.empty()) { r.append(u'#'); r.append(fragment); }
        return r;
    }

    knst_c16string request_target() const {
        knst_c16string raw;
        if (path.empty()) raw.append(u"/"); else raw.append(path);
        if (!query.empty()) { raw.append(u'?'); raw.append(query); }
        return knst_net_detail::encode_target(raw);
    }

    knst_c16string host_bracketed() const {
        knst_c16string r;
        bool v6 = false;
        for (uint32_t k = 0; k < host.length(); ++k) if (host[k] == u':') { v6 = true; break; }
        if (v6) { r.append(u'['); r.append(host); r.append(u']'); } else r.append(host);
        return r;
    }

    knst_c16string host_header_value() const {
        knst_c16string r;
        if (!is_valid()) return r;
        r = host_bracketed();
        uint16_t def = (scheme == u"https") ? 443 : (scheme == u"http") ? 80 : 0;
        if (port != 0 && port != def) { r.append(u':'); knst_net_detail::append_uint(r, port); }
        return r;
    }

    // RFC 3986 §5.2.4
    static knst_c16string remove_dot_segments(const knst_c16string& input) {
        knst_vector<char16_t> out;
        uint32_t i = 0;
        auto pop_last_segment = [&]() {
            while (!out.empty() && out.back() != u'/') out.pop_back();
            if (!out.empty()) out.pop_back();
        };
        while (i < input.length()) {
            uint32_t rem = static_cast<uint32_t>(input.length() - i);
            if (rem >= 3 && input[i] == u'.' && input[i+1] == u'.' && input[i+2] == u'/') { i += 3; continue; }
            if (rem >= 2 && input[i] == u'.' && input[i+1] == u'/') { i += 2; continue; }
            if (rem >= 3 && input[i] == u'/' && input[i+1] == u'.' && input[i+2] == u'/') { i += 2; continue; }
            if (rem == 2 && input[i] == u'/' && input[i+1] == u'.') { out.push_back(u'/'); i += 2; continue; }
            if (rem >= 4 && input[i] == u'/' && input[i+1] == u'.' && input[i+2] == u'.' && input[i+3] == u'/') {
                pop_last_segment(); i += 3; continue;
            }
            if (rem == 3 && input[i] == u'/' && input[i+1] == u'.' && input[i+2] == u'.') {
                pop_last_segment(); out.push_back(u'/'); i += 3; continue;
            }
            if (rem == 1 && input[i] == u'.') { i += 1; continue; }
            if (rem == 2 && input[i] == u'.' && input[i+1] == u'.') { i += 2; continue; }
            out.push_back(input[i]);
            ++i;
            while (i < input.length() && input[i] != u'/') { out.push_back(input[i]); ++i; }
        }
        knst_c16string result;
        for (uint32_t k = 0; k < out.size(); ++k) result.append(out[k]);
        return result;
    }

    // RFC 3986 §5.2 reference resolution
    static bool resolve(const knst_url& base, const knst_c16string& ref,
                        knst_url& out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!base.is_valid()) { err = knst_network_error::HttpParseUrl; return false; }
        if (ref.empty()) { out = base; return true; }
        if (knst_net_detail::has_scheme(ref)) return knst_url::parse(ref, out, err);

        out = base;
        if (ref.length() >= 2 && ref[0] == u'/' && ref[1] == u'/') {
            knst_c16string abs_ref = base.scheme;
            abs_ref.append(u':');
            abs_ref.append(ref);
            return knst_url::parse(abs_ref, out, err);
        }

        const uint32_t NPOS = static_cast<uint32_t>(-1);
        uint32_t frag_pos = NPOS;
        for (uint32_t i = 0; i < ref.length(); ++i) if (ref[i] == u'#') { frag_pos = i; break; }
        uint32_t head_end = frag_pos != NPOS ? frag_pos : ref.length();

        knst_c16string rpath, rquery;
        bool has_query = false;
        uint32_t query_pos = NPOS;
        for (uint32_t i = 0; i < head_end; ++i) if (ref[i] == u'?') { query_pos = i; break; }
        if (query_pos != NPOS) {
            has_query = true;
            rpath  = ref.substr(0, query_pos);
            rquery = ref.substr(query_pos + 1, head_end - query_pos - 1);
        } else {
            rpath = ref.substr(0, head_end);
        }
        out.fragment = frag_pos != NPOS ? ref.substr(frag_pos + 1, ref.length() - frag_pos - 1)
                                        : knst_c16string();

        if (rpath.length() > 0 && rpath[0] == u'/') {
            out.path = remove_dot_segments(rpath);
            out.query = rquery;
            return true;
        }
        if (rpath.empty()) {
            if (has_query) out.query = rquery;       // "?" alone => empty query (RFC 3986 §5.2.2)
            return true;
        }
        knst_c16string merged = base.path;
        uint32_t last_slash = NPOS;
        for (uint32_t i = 0; i < merged.length(); ++i) if (merged[i] == u'/') last_slash = i;
        if (last_slash != NPOS) merged = merged.substr(0, last_slash + 1);
        else                    merged = knst_c16string(u"/");
        merged.append(rpath);
        out.path  = remove_dot_segments(merged);
        out.query = rquery;
        return true;
    }
};


/* ============================================================================
 * 13) HTTP request / response
 * ============================================================================ */

struct knst_http_header {
    knst_c16string name;
    knst_c16string value;
};

class knst_http_request {
public:
    knst_c16string method = u"GET";
    knst_c16string path   = u"/";
    knst_vector<knst_http_header> headers;
    knst_byte_string body;
    // Optional chunked-upload trailers (RFC 7230 §4.1.2). Only emitted when
    // Transfer-Encoding: chunked is set via set_header_internal().
    knst_vector<knst_http_header> trailers;

    static bool ieq(const knst_c16string& a, const knst_c16string& b) noexcept {
        return knst_net_detail::ieq(a, b);
    }

    // User-facing: rejects reserved headers
    bool set_header(const knst_c16string& name, const knst_c16string& value) {
        if (ieq(name, u"Host") || ieq(name, u"Content-Length") ||
            ieq(name, u"Transfer-Encoding") || ieq(name, u"Connection") ||
            ieq(name, u"TE") || ieq(name, u"Trailer") || ieq(name, u"Upgrade") ||
            ieq(name, u"Expect") || ieq(name, u"Proxy-Connection") ||
            ieq(name, u"Proxy-Authorization") || ieq(name, u"Accept-Encoding")) {
            return false;
        }
        return set_header_internal(name, value);
    }

    // Internal: allows reserved (library sets them safely)
    bool set_header_internal(const knst_c16string& name, const knst_c16string& value) {
        if (!knst_net_detail::valid_header_name(name) ||
            !knst_net_detail::valid_header_value(value)) return false;
        for (uint32_t i = 0; i < headers.size(); ++i) {
            if (ieq(headers[i].name, name)) { headers[i].value = value; return true; }
        }
        knst_http_header h; h.name = name; h.value = value;
        return headers.push_back(h);
    }

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool set_json(const knst_json& j, uint32_t indent = 0) noexcept {
        knst_json_buffer buf;
        knst_json_dump_options opt;
        opt.indent = indent;
        if (!j.dump(buf, opt)) return false;

                body.clear();
        body.resize(static_cast<uint32_t>(buf.size()));
        if (body.length() != buf.size()) return false;
        if (buf.size() > 0) {
            ::memcpy(&body[0], buf.data(), buf.size());
        }
        return set_header_internal(u"Content-Type", u"application/json");
    }
#endif

    bool has_header(const knst_c16string& name) const {
        for (uint32_t i = 0; i < headers.size(); ++i)
            if (ieq(headers[i].name, name)) return true;
        return false;
    }
    knst_c16string get_header(const knst_c16string& name) const {
        for (uint32_t i = 0; i < headers.size(); ++i)
            if (ieq(headers[i].name, name)) return headers[i].value;
        return knst_c16string();
    }

    // Request line + headers + blank line (no body).
    knst_byte_string serialize_head() const {
        knst_byte_string out;
        auto ascii = [&](const char* s) { for (; *s; ++s) out.push_back(static_cast<uint8_t>(*s)); };
        auto raw = [&](const knst_c16string& s) {
            knst_byte_string u = knst_net_detail::to_utf8(s);
            knst_net_detail::append_bytes(out, u.data(), u.length());
        };
        raw(method); ascii(" "); raw(path); ascii(" HTTP/1.1\r\n");
        for (uint32_t i = 0; i < headers.size(); ++i) {
            raw(headers[i].name); ascii(": "); raw(headers[i].value); ascii("\r\n");
        }
        ascii("\r\n");
        return out;
    }

    knst_byte_string serialize() const {
        knst_c16string te = get_header(u"Transfer-Encoding");
        if (!ieq(knst_net_detail::trim16(te), u"chunked")) {
            knst_byte_string out = serialize_head();
            knst_net_detail::append_bytes(out, body.data(), body.length());
            return out;
        }
        // Chunked path: head + one big chunk + zero chunk + optional trailers.
        knst_byte_string out = serialize_head();
        if (body.length() > 0) {
            char sz[24];
            int n = ::snprintf(sz, sizeof(sz), "%x\r\n", static_cast<unsigned>(body.length()));
            for (int i = 0; i < n; ++i) out.push_back(static_cast<uint8_t>(sz[i]));
            knst_net_detail::append_bytes(out, body.data(), body.length());
            out.push_back('\r'); out.push_back('\n');
        }
        out.push_back('0'); out.push_back('\r'); out.push_back('\n');
        for (uint32_t i = 0; i < trailers.size(); ++i) {
            knst_byte_string n8 = knst_net_detail::to_utf8(trailers[i].name);
            knst_byte_string v8 = knst_net_detail::to_utf8(trailers[i].value);
            knst_net_detail::append_bytes(out, n8.data(), n8.length());
            out.push_back(':'); out.push_back(' ');
            knst_net_detail::append_bytes(out, v8.data(), v8.length());
            out.push_back('\r'); out.push_back('\n');
        }
        out.push_back('\r'); out.push_back('\n');
        return out;
    }
};

class knst_http_response {
public:
    uint16_t status = 0;
    knst_c16string reason;
    knst_c16string version;
    knst_vector<knst_http_header> headers;
    knst_byte_string body;

    bool is_success()    const noexcept { return status >= 200 && status < 300; }
    bool is_redirect()   const noexcept {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }
    bool is_client_err() const noexcept { return status >= 400 && status < 500; }
    bool is_server_err() const noexcept { return status >= 500 && status < 600; }

    knst_c16string get_header(const knst_c16string& name) const {
        for (uint32_t i = 0; i < headers.size(); ++i)
            if (ieq(headers[i].name, name)) return headers[i].value;
        return knst_c16string();
    }

    // true if any header `name` has comma-separated token `token` (case-insens.)
    bool header_has_token(const knst_c16string& name, const knst_c16string& token) const {
        for (uint32_t i = 0; i < headers.size(); ++i) {
            if (!ieq(headers[i].name, name)) continue;
            const knst_c16string& v = headers[i].value;
            uint32_t start = 0;
            for (uint32_t k = 0; k <= v.length(); ++k) {
                if (k == v.length() || v[k] == u',') {
                    if (ieq(knst_net_detail::trim16(v.substr(start, k - start)), token)) return true;
                    start = k + 1;
                }
            }
        }
        return false;
    }

#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json json() const noexcept {
        if (body.empty() || !body.data()) return knst_json();
        knst_json j;
        knst_json_error e;
        if (!knst_json::parse(reinterpret_cast<const char*>(body.data()),
                              body.length(), j, &e)) {
            return knst_json();
        }
        return j;
    }
#endif

    static bool ieq(const knst_c16string& a, const knst_c16string& b) noexcept {
        return knst_net_detail::ieq(a, b);
    }

    // Strict status line + header parse of a complete header block
    // (ending with CRLFCRLF). Body bytes after it (if any) go to `body`.
    static bool parse(const knst_byte_string& raw, knst_http_response& out,
                      knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out = knst_http_response{};

        uint32_t pos = 0, eol = 0;
        if (!find_crlf(raw, 0, eol) || eol < 12) { err = knst_network_error::HttpMalformedStatus; return false; }
        if (raw[0] != 'H' || raw[1] != 'T' || raw[2] != 'T' || raw[3] != 'P' || raw[4] != '/' ||
            raw[5] != '1' || raw[6] != '.' || raw[7] < '0' || raw[7] > '9' || raw[8] != ' ') {
            err = knst_network_error::HttpMalformedStatus; return false;
        }
        const uint32_t sp1 = 8;
        if (sp1 + 4 > eol) { err = knst_network_error::HttpMalformedStatus; return false; }
        out.version = c16_ascii(raw, 0, sp1);

        uint32_t code = 0;
        for (uint32_t k = 0; k < 3; ++k) {
            uint8_t d = raw[sp1 + 1 + k];
            if (d < '0' || d > '9') { err = knst_network_error::HttpMalformedStatus; return false; }
            code = code * 10 + (d - '0');
        }
        if (code < 100) { err = knst_network_error::HttpMalformedStatus; return false; }
        out.status = static_cast<uint16_t>(code);
        uint32_t after = sp1 + 4;
        if (after < eol) {
            if (raw[after] != ' ') { err = knst_network_error::HttpMalformedStatus; return false; }
            for (uint32_t k = after + 1; k < eol; ++k)
                if (raw[k] == 0 || raw[k] == '\r' || raw[k] == '\n') {
                    err = knst_network_error::HttpMalformedStatus; return false;
                }
            out.reason = c16_ascii(raw, after + 1, eol - after - 1);
        }
        pos = eol + 2;

        bool found_end = false;
        while (pos + 1 < raw.length()) {
            if (raw[pos] == '\r' && raw[pos + 1] == '\n') { pos += 2; found_end = true; break; }
            uint32_t he = 0, colon = 0;
            if (!find_crlf(raw, pos, he) || !find_byte_in(raw, pos, he, ':', colon) || colon == pos) {
                err = knst_network_error::HttpMalformedHeaders; return false;
            }
            for (uint32_t k = pos; k < colon; ++k) {
                if (raw[k] <= ' ' || raw[k] >= 0x7F) { err = knst_network_error::HttpMalformedHeaders; return false; }
            }
            uint32_t vs = colon + 1, ve = he;
            for (uint32_t k = vs; k < ve; ++k) {
                if (raw[k] == 0 || raw[k] == '\r' || raw[k] == '\n') {
                    err = knst_network_error::HttpMalformedHeaders; return false;
                }
            }
            while (vs < ve && (raw[vs] == ' ' || raw[vs] == '\t')) ++vs;
            while (ve > vs && (raw[ve - 1] == ' ' || raw[ve - 1] == '\t')) --ve;
            knst_http_header h;
            h.name  = c16_ascii(raw, pos, colon - pos);
            h.value = c16_ascii(raw, vs, ve - vs);
            if (out.headers.size() >= 512) { err = knst_network_error::HttpMalformedHeaders; return false; }
            if (!out.headers.push_back(h)) { err = knst_network_error::OutOfMemory; return false; }
            pos = he + 2;
        }
        if (!found_end) { err = knst_network_error::HttpMalformedHeaders; return false; }

        // Reject conflicting CL, or CL+TE (smuggling guard)
        {
            uint32_t cl_count = 0;
            knst_c16string cl_value;
            bool has_te = false;
            for (uint32_t i = 0; i < out.headers.size(); ++i) {
                if (ieq(out.headers[i].name, u"Content-Length")) {
                    ++cl_count;
                    if (cl_count == 1) cl_value = out.headers[i].value;
                    else if (out.headers[i].value != cl_value) {
                        err = knst_network_error::HttpMalformedHeaders; return false;
                    }
                }
                if (ieq(out.headers[i].name, u"Transfer-Encoding")) has_te = true;
            }
            if (cl_count > 0 && has_te) { err = knst_network_error::HttpMalformedHeaders; return false; }
        }

        for (uint32_t i = pos; i < raw.length(); ++i) out.body.push_back(raw[i]);
        return true;
    }

private:
    static bool find_byte_in(const knst_byte_string& s, uint32_t from, uint32_t to,
                             char c, uint32_t& out) noexcept {
        for (uint32_t i = from; i < to && i < s.length(); ++i)
            if (s[i] == static_cast<uint8_t>(c)) { out = i; return true; }
        return false;
    }
    static bool find_crlf(const knst_byte_string& s, uint32_t from, uint32_t& out) noexcept {
        for (uint32_t i = from; i + 1 < s.length(); ++i)
            if (s[i] == '\r' && s[i + 1] == '\n') { out = i; return true; }
        return false;
    }
    static knst_c16string c16_ascii(const knst_byte_string& s, uint32_t start, uint32_t len) {
        knst_c16string r;
        for (uint32_t i = start; i < start + len && i < s.length(); ++i)
            r.append(static_cast<char16_t>(s[i]));
        return r;
    }
};


/* ============================================================================
 * 14) knst_cookie
 * ============================================================================ */

struct knst_cookie {
    knst_c16string name, value, domain, path;
    int64_t expires_unix = 0;   // 0 = session; >0 = absolute unix seconds
    bool    secure = false;
    bool    host_only = true;
};


/* ============================================================================
 * 15) TLS (OpenSSL)
 * ============================================================================ */

#if defined(KNST_NETWORK_ENABLE_TLS)

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    #error "knst_network.hpp: OpenSSL >= 1.1.0 (or LibreSSL 2.7+) is required"
#endif

#if KNST_USING_PLATFORM_WINDOWS && !defined(KNST_NETWORK_NO_WIN_CERTSTORE)
    #include <wincrypt.h>
    // wincrypt.h macros collide with OpenSSL names
    #undef X509_NAME
    #undef X509_EXTENSIONS
    #undef X509_CERT_PAIR
    #undef PKCS7_ISSUER_AND_SERIAL
    #undef OCSP_REQUEST
    #undef OCSP_RESPONSE
    #ifdef _MSC_VER
        #pragma comment(lib, "crypt32.lib")
    #endif
    #define KNST_NETWORK_USE_WIN_CERTSTORE 1
#else
    #define KNST_NETWORK_USE_WIN_CERTSTORE 0
#endif

namespace knst_net_detail {
        struct tls_state {
        std::mutex m;
        SSL_CTX*   ctx     = nullptr;
        bool       created = false;
        char       ca_file[512] = {};
        char       ca_path[512] = {};
        char       client_cert[512] = {};   // mTLS client certificate (PEM)
        char       client_key[512]  = {};   // mTLS private key (PEM)
        bool       skip_verify = false;     // for self-signed IPP printers
    };
    inline tls_state& tls_st() noexcept { static tls_state s; return s; }

    inline bool tls_copy_path(char* dst, const char* src) noexcept {
        if (!src) return false;
        size_t l = ::strlen(src);
        if (l == 0 || l >= 512) return false;
        ::memcpy(dst, src, l + 1);
        return true;
    }

    // Blocks SIGPIPE on this thread for the guard's lifetime (SSL_connect /
    // SSL_write / SSL_shutdown use send() without MSG_NOSIGNAL). macOS uses
    // SO_NOSIGPIPE instead; Windows has no SIGPIPE.
    #if !KNST_USING_PLATFORM_WINDOWS && !defined(__APPLE__)
    struct sigpipe_guard {
        sigset_t old_mask;
        bool active = false, was_pending = false;
        sigpipe_guard() noexcept {
            sigset_t s; sigemptyset(&s); sigaddset(&s, SIGPIPE);
            sigset_t pend; sigemptyset(&pend);
            if (sigpending(&pend) == 0) was_pending = sigismember(&pend, SIGPIPE) == 1;
            active = pthread_sigmask(SIG_BLOCK, &s, &old_mask) == 0;
        }
        ~sigpipe_guard() noexcept {
            if (!active) return;
            if (!was_pending) {
                sigset_t pend; sigemptyset(&pend);
                if (sigpending(&pend) == 0 && sigismember(&pend, SIGPIPE) == 1) {
                    sigset_t s; sigemptyset(&s); sigaddset(&s, SIGPIPE);
                    struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 0;
                    while (sigtimedwait(&s, nullptr, &ts) < 0 && errno == EINTR) {}
                }
            }
            pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        }
    };
    #else
    struct sigpipe_guard { sigpipe_guard() noexcept {} };
    #endif

    // Process-wide TLS session cache (client side) => abbreviated handshakes.
    struct tls_sess_cache {
        struct ent { char key[320]; SSL_SESSION* s; ent* next; };
        std::mutex m;
        ent* head = nullptr;
        uint32_t count = 0;
        static constexpr uint32_t MAX_ENTRIES = 64;

        void put(const char* key, SSL_SESSION* s) noexcept {      // takes ownership of s
            ent* n = new (std::nothrow) ent();
            if (!n) { SSL_SESSION_free(s); return; }
            size_t l = ::strlen(key); if (l >= sizeof(n->key)) l = sizeof(n->key) - 1;
            ::memcpy(n->key, key, l); n->key[l] = '\0'; n->s = s; n->next = nullptr;
            ent* dead = nullptr;
            {
                std::lock_guard<std::mutex> lk(m);
                ent* prev = nullptr;
                for (ent* e = head; e; prev = e, e = e->next)
                    if (::strcmp(e->key, n->key) == 0) {
                        if (prev) prev->next = e->next; else head = e->next;
                        --count; e->next = dead; dead = e; break;
                    }
                n->next = head; head = n; ++count;
                if (count > MAX_ENTRIES) {
                    ent* p2 = nullptr; ent* t = head;
                    while (t && t->next) { p2 = t; t = t->next; }
                    if (t && p2) { p2->next = nullptr; --count; t->next = dead; dead = t; }
                }
            }
            while (dead) { ent* nx = dead->next; SSL_SESSION_free(dead->s); delete dead; dead = nx; }
        }
        // Returns a new reference (caller frees) or nullptr.
        SSL_SESSION* get(const char* key) noexcept {
            std::lock_guard<std::mutex> lk(m);
            for (ent* e = head; e; e = e->next)
                if (::strcmp(e->key, key) == 0) { SSL_SESSION_up_ref(e->s); return e->s; }
            return nullptr;
        }
    };
    inline tls_sess_cache& tls_sess() noexcept {
        static tls_sess_cache* c = new tls_sess_cache();     // intentionally leaked (shutdown-order safe)
        return *c;
    }
    inline int tls_sess_idx() noexcept {
        static int idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
        return idx;
    }
    inline int tls_new_session_cb(SSL* ssl, SSL_SESSION* sess) noexcept {
        const char* key = static_cast<const char*>(SSL_get_ex_data(ssl, tls_sess_idx()));
        if (!key || !*key) return 0;
        tls_sess().put(key, sess);
        return 1;                                            // we own the reference now
    }

    inline SSL_CTX* tls_get_ctx() noexcept {
        tls_state& s = tls_st();
        std::lock_guard<std::mutex> lk(s.m);
        if (s.ctx) return s.ctx;
        if (s.created) return nullptr;          // earlier init failed
        s.created = true;

        OPENSSL_init_ssl(0, nullptr);
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) return nullptr;
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_verify(ctx, s.skip_verify ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
        {
            long opts = 0;
        #ifdef SSL_OP_NO_COMPRESSION
            opts |= SSL_OP_NO_COMPRESSION;                    // CRIME
        #endif
        #ifdef SSL_OP_NO_RENEGOTIATION
            opts |= SSL_OP_NO_RENEGOTIATION;                  // 1.1.1+
        #endif
            if (opts) SSL_CTX_set_options(ctx, opts);
        }
                // ALPN: announce HTTP/1.1 today; when HTTP/2 lands, prepend "h2".
        {
            static const unsigned char alpn[] = {
                8, 'h','t','t','p','/','1','.','1'
            };
            ::SSL_CTX_set_alpn_protos(ctx, alpn, sizeof(alpn));
        }
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
        SSL_CTX_sess_set_new_cb(ctx, tls_new_session_cb);

        bool custom = s.ca_file[0] != '\0' || s.ca_path[0] != '\0';
        if (custom) {
            if (SSL_CTX_load_verify_locations(ctx,
                    s.ca_file[0] ? s.ca_file : nullptr,
                    s.ca_path[0] ? s.ca_path : nullptr) != 1) {
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
        // mTLS: load client certificate + private key if configured.
        if (s.client_cert[0] && s.client_key[0]) {
            if (SSL_CTX_use_certificate_chain_file(ctx, s.client_cert) != 1 ||
                SSL_CTX_use_PrivateKey_file(ctx, s.client_key, SSL_FILETYPE_PEM) != 1 ||
                SSL_CTX_check_private_key(ctx) != 1) {
                SSL_CTX_free(ctx); ERR_clear_error();
                s.ctx = nullptr;
                return nullptr;
            }
        }
        ERR_clear_error();
        s.ctx = ctx;
        return ctx;
    }
} // namespace knst_net_detail

// Must be called BEFORE the first TLS connection. Returns false if too late.
// PEM-encoded client certificate + private key for mutual TLS (mTLS).
inline bool knst_tls_set_client_cert(const char* cert_pem_path, const char* key_pem_path) noexcept {
    auto& s = knst_net_detail::tls_st();
    std::lock_guard<std::mutex> lk(s.m);
    if (s.created) return false;
    if (!cert_pem_path || !key_pem_path) return false;
    return knst_net_detail::tls_copy_path(s.client_cert, cert_pem_path) &&
           knst_net_detail::tls_copy_path(s.client_key, key_pem_path);
}

// Must be called BEFORE the first TLS connection. Returns false if too late.
inline bool knst_tls_set_ca_file(const char* path) noexcept {
    auto& s = knst_net_detail::tls_st();
    std::lock_guard<std::mutex> lk(s.m);
    if (s.created) return false;
    return knst_net_detail::tls_copy_path(s.ca_file, path);
}
inline bool knst_tls_set_ca_path(const char* dir) noexcept {
    auto& s = knst_net_detail::tls_st();
    std::lock_guard<std::mutex> lk(s.m);
    if (s.created) return false;
    return knst_net_detail::tls_copy_path(s.ca_path, dir);
}

// Disable certificate verification. Use ONLY for trusted LAN devices
// (e.g. self-signed IPP printers). Must be called BEFORE the first TLS handshake.
inline bool knst_tls_set_skip_verify(bool skip) noexcept {
    auto& s = knst_net_detail::tls_st();
    std::lock_guard<std::mutex> lk(s.m);
    s.skip_verify = skip;
    // Also apply immediately if ctx already exists (e.g. HTTP test ran before).
    if (s.ctx) {
        SSL_CTX_set_verify(s.ctx, skip ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
    }
    return true;
}

class knst_ssl_socket : public knst_tcp_socket {
public:
    using knst_tcp_socket::recv;     // keep recv(knst_byte_string&, ...) visible

    knst_ssl_socket() noexcept = default;
    ~knst_ssl_socket() noexcept override { knst_ssl_socket::close(); }
    knst_ssl_socket(const knst_ssl_socket&) = delete;
    knst_ssl_socket& operator=(const knst_ssl_socket&) = delete;
    knst_ssl_socket(knst_ssl_socket&&) = delete;
    knst_ssl_socket& operator=(knst_ssl_socket&&) = delete;

    // Number of SSL_write() calls made process-wide (regression-test hook).
    static std::atomic<uint64_t>& write_call_counter() noexcept {
        static std::atomic<uint64_t> c{0}; return c;
    }

    bool is_tls_active() const noexcept { return m_ssl != nullptr; }

    // true while decrypted bytes are buffered inside OpenSSL (the raw socket
    // may look idle although a read would not block).
    bool has_pending_input() const noexcept override { return m_ssl && SSL_pending(m_ssl) > 0; }

    // DNS + TCP + handshake inside one overall deadline (0 = infinite).
    bool connect_tls(const knst_c16string& host, uint16_t port,
                     uint32_t dns_timeout_ms, uint32_t timeout_ms,
                     knst_network_error& err) noexcept {
        err = knst_network_error::None;
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        knst_vector<knst_endpoint> eps;
        if (!knst_dns_resolver::resolve_async(host, port, eps, err,
                knst_net_detail::dns_budget(dns_timeout_ms, dl)))
            return false;
        bool expired; uint32_t rem = dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; return false; }
        return connect_tls_any(eps, host, rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem, err);
    }
    bool connect_tls(const knst_c16string& host, uint16_t port,
                     uint32_t timeout_ms, knst_network_error& err) noexcept {
        return connect_tls(host, port, timeout_ms, timeout_ms, err);
    }

    bool connect_tls_any(const knst_vector<knst_endpoint>& eps, const knst_c16string& sni_host,
                         uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (eps.size() == 0) { err = knst_network_error::DnsNoData; return false; }
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        for (uint32_t i = 0; i < eps.size(); ++i) {
            bool expired; uint32_t rem = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; return false; }
            uint32_t budget = knst_net_detail::attempt_budget(rem, eps.size() - i);
            if (!connect(eps[i], budget, err)) continue;
            rem = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; close(); return false; }
            if (tls_handshake(sni_host, rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem, err)) return true;
            if (err == knst_network_error::TlsCertificateInvalid) return false;   // another address won't fix it
        }
        return false;
    }

    bool connect_tls(const knst_endpoint& ep, const knst_c16string& sni_host,
                     uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        if (!connect(ep, timeout_ms, err)) return false;
        bool expired; uint32_t rem = dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; close(); return false; }
        return tls_handshake(sni_host, rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem, err);
    }

    // TLS handshake over an already-connected TCP socket (also used after a
    // proxy CONNECT tunnel). Runs non-blocking under ONE deadline
    // (timeout_ms, 0 = infinite). On failure the socket is closed.
    bool tls_handshake(const knst_c16string& sni_host, uint32_t timeout_ms,
                       knst_network_error& err, uint16_t port_hint = 0) noexcept {
        err = knst_network_error::None;
        if (!is_open()) { err = knst_network_error::NotOpen; return false; }
        m_rcv_cached = m_snd_cached = 0xFFFFFFFEu;
        if (m_ssl) { SSL_free(m_ssl); m_ssl = nullptr; }
        m_fatal = false;

        char hbuf[256] = {};
        if (sni_host.empty() || sni_host.length() >= sizeof(hbuf)) {
            err = knst_network_error::InvalidArgument; close(); return false;
        }
        for (uint32_t i = 0; i < sni_host.length(); ++i) {
            char16_t c = sni_host[i];
            if (c <= 0x20 || c >= 0x7F) { err = knst_network_error::InvalidArgument; close(); return false; }
            hbuf[i] = static_cast<char>(c);
        }

        SSL_CTX* ctx = knst_net_detail::tls_get_ctx();
        if (!ctx) { err = knst_network_error::TlsHandshakeFailed; close(); return false; }

        knst_net_detail::set_blocking(native_handle(), false);

        m_ssl = SSL_new(ctx);
        if (!m_ssl) { err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); close(); return false; }

        // IP literal: verify against iPAddress SAN, no SNI. Name: SNI + hostname check.
        bool is_ip = false;
        {
            unsigned char tmp[sizeof(struct in6_addr)];
            is_ip = ::inet_pton(AF_INET, hbuf, tmp) == 1 || ::inet_pton(AF_INET6, hbuf, tmp) == 1;
        }
        if (is_ip) {
            X509_VERIFY_PARAM* vp = SSL_get0_param(m_ssl);
            if (!vp || X509_VERIFY_PARAM_set1_ip_asc(vp, hbuf) != 1) {
                err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); close(); return false;
            }
        } else {
            SSL_set_tlsext_host_name(m_ssl, hbuf);
            SSL_set_hostflags(m_ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            if (SSL_set1_host(m_ssl, hbuf) != 1) {
                err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); close(); return false;
            }
        }
        if (SSL_set_fd(m_ssl, static_cast<int>(native_handle())) != 1) {
            err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); close(); return false;
        }

        {   // session resumption key: lower(host):port
            uint16_t port = port_hint ? port_hint : remote_endpoint().port;
            char* k = m_sess_key; size_t n = 0;
            for (const char* h = hbuf; *h && n + 8 < sizeof(m_sess_key); ++h)
                k[n++] = static_cast<char>((*h >= 'A' && *h <= 'Z') ? (*h - 'A' + 'a') : *h);
            ::snprintf(k + n, sizeof(m_sess_key) - n, ":%u", static_cast<unsigned>(port));
            SSL_set_ex_data(m_ssl, knst_net_detail::tls_sess_idx(), m_sess_key);
            if (SSL_SESSION* sess = knst_net_detail::tls_sess().get(m_sess_key)) {
                SSL_set_session(m_ssl, sess);
                SSL_SESSION_free(sess);
            }
        }

        auto dl = knst_net_detail::deadline::after(
            timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : timeout_ms);
        {
            knst_net_detail::sigpipe_guard sp;
            for (;;) {
                ERR_clear_error();
                int rc = SSL_connect(m_ssl);
                if (rc == 1) break;
                int se  = SSL_get_error(m_ssl, rc);
                int sys = knst_net_detail::last_socket_error();
                if (se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE) {
                    knst_network_error perr = knst_network_error::None;
                    if (knst_net_detail::poll_wait(native_handle(), se == SSL_ERROR_WANT_READ, dl, perr))
                        continue;
                    err = perr;                                    // Timeout / socket error
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
                m_fatal = true;
                close();
                return false;
            }
        }
        // Data phase: blocking socket + per-syscall SO_RCVTIMEO / SO_SNDTIMEO.
        knst_net_detail::set_blocking(native_handle(), true);
        m_last_error = knst_network_error::None;
        return true;
    }

    // ---- virtual I/O overrides (plain fallback while TLS is not active) ----

    bool send(const void* data, size_t len, size_t& sent, uint32_t timeout_ms,
              knst_network_error& err) noexcept override {
        if (!m_ssl) return knst_tcp_socket::send(data, len, sent, timeout_ms, err);
        sent = 0; err = knst_network_error::None;
        if (!data && len) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }
        if (len == 0) return true;

        set_snd_timeout(knst_net_detail::resolve_timeout(timeout_ms, m_write_timeout_ms));
        // Bounded chunk: lets send_all() re-check its overall deadline regularly.
        const size_t MAX_CHUNK = size_t(256) * 1024;
        int chunk = static_cast<int>(len > MAX_CHUNK ? MAX_CHUNK : len);
        knst_net_detail::sigpipe_guard sp;
        for (;;) {
            ERR_clear_error();
            write_call_counter().fetch_add(1, std::memory_order_relaxed);
            int n = SSL_write(m_ssl, data, chunk);
            if (n > 0) { sent = static_cast<size_t>(n); return true; }
            if (map_ssl_error(n, err)) continue;     // EINTR
            m_last_error = err;
            return false;
        }
    }

    bool recv(void* buf, size_t cap, size_t& got, uint32_t timeout_ms,
              knst_network_error& err) noexcept override {
        if (!m_ssl) return knst_tcp_socket::recv(buf, cap, got, timeout_ms, err);
        got = 0; err = knst_network_error::None;
        if (!buf || cap == 0) { err = knst_network_error::InvalidArgument; m_last_error = err; return false; }

        set_rcv_timeout(knst_net_detail::resolve_timeout(timeout_ms, m_read_timeout_ms));
        int c = static_cast<int>(cap > static_cast<size_t>(INT_MAX) ? INT_MAX : cap);
        for (;;) {
            ERR_clear_error();
            int n = SSL_read(m_ssl, buf, c);
            if (n > 0) { got = static_cast<size_t>(n); return true; }
            if (SSL_get_error(m_ssl, n) == SSL_ERROR_ZERO_RETURN) {   // clean close_notify
                ERR_clear_error();
                return true;                                          // got == 0 => EOF
            }
            if (map_ssl_error(n, err)) continue;
            m_last_error = err;
            return false;
        }
    }

    void close() noexcept override {
        if (m_ssl) {
            if (native_handle() != KNST_INVALID_SOCKET && !m_fatal) {
                set_snd_timeout(1000);                  // never hang on close_notify
                knst_net_detail::sigpipe_guard sp;
                ERR_clear_error();
                SSL_shutdown(m_ssl);      // best effort, sends close_notify
                ERR_clear_error();
            }
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        m_fatal = false;
        m_rcv_cached = m_snd_cached = 0xFFFFFFFEu;
        knst_tcp_socket::close();
    }

    bool session_reused() const noexcept { return m_ssl && SSL_session_reused(m_ssl) == 1; }

private:
    // setsockopt only when the value changes (avoids a syscall per read/write)
    void set_rcv_timeout(uint32_t ms) noexcept {
        if (ms == knst_net_detail::KNST_NET_INFINITE) ms = 0;
        if (ms != m_rcv_cached) { knst_net_detail::set_so_recv_timeout(native_handle(), ms); m_rcv_cached = ms; }
    }
    void set_snd_timeout(uint32_t ms) noexcept {
        if (ms == knst_net_detail::KNST_NET_INFINITE) ms = 0;
        if (ms != m_snd_cached) { knst_net_detail::set_so_send_timeout(native_handle(), ms); m_snd_cached = ms; }
    }

    // Returns true => retry (interrupted). Otherwise fills err.
    bool map_ssl_error(int ret, knst_network_error& err) noexcept {
        int se  = SSL_get_error(m_ssl, ret);
        int sys = knst_net_detail::last_socket_error();
        if (se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE) {
            err = knst_network_error::Timeout;          // blocking socket => SO_*TIMEO expired
            m_fatal = true;
        } else if (se == SSL_ERROR_SYSCALL) {
            knst_network_error e = knst_net_detail::map_errno(sys);
            if (e == knst_network_error::Interrupted) { ERR_clear_error(); return true; }
            if (e == knst_network_error::None)        e = knst_network_error::ConnectionClosed;
            if (e == knst_network_error::WouldBlock || e == knst_network_error::ConnectionTimedOut)
                e = knst_network_error::Timeout;
            err = e; m_fatal = true;
        } else {
            err = knst_network_error::ConnectionClosed; m_fatal = true;
        }
        ERR_clear_error();
        return false;
    }

    SSL* m_ssl   = nullptr;
    bool m_fatal = false;       // skip SSL_shutdown on broken connections
    uint32_t m_rcv_cached = 0xFFFFFFFEu, m_snd_cached = 0xFFFFFFFEu;
    char m_sess_key[320] = {};
};

#endif // KNST_NETWORK_ENABLE_TLS


/* ============================================================================
 * 16) knst_connection_pool — keep-alive socket reuse (thread-safe)
 * ============================================================================ */

class knst_connection_pool {
public:
    knst_connection_pool() noexcept = default;
    ~knst_connection_pool() noexcept { clear(); }
    knst_connection_pool(const knst_connection_pool&) = delete;
    knst_connection_pool& operator=(const knst_connection_pool&) = delete;

    // Returns an idle, still-alive socket for `key` (ownership to caller) or nullptr.
    knst_tcp_socket* take(const knst_c16string& key, uint32_t idle_ms) noexcept {
        auto now = std::chrono::steady_clock::now();
        node* dead = nullptr;
        node* found = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            node* prev = nullptr;
            node* cur = m_head;
            while (cur) {
                node* next = cur->next;
                bool stale = std::chrono::duration_cast<std::chrono::milliseconds>(now - cur->last).count()
                             > static_cast<long long>(idle_ms);
                bool match = !found && cur->key == key;
                bool drop = stale || (match && !alive(cur->sock));
                if (drop || match) {
                    if (prev) prev->next = next; else m_head = next;
                    --m_count;
                    if (drop) { cur->next = dead; dead = cur; }
                    else      { found = cur; }
                } else {
                    prev = cur;
                }
                cur = next;
            }
        }
        destroy_list(dead);
        if (!found) return nullptr;
        knst_tcp_socket* s = found->sock;
        delete found;
        return s;
    }

    // Takes ownership on success (true). On false the caller must delete.
    bool put(const knst_c16string& key, knst_tcp_socket* s, uint32_t max_per_host) noexcept {
        if (!s || max_per_host == 0 || !s->is_open()) return false;
        node* n = new (std::nothrow) node();
        if (!n) return false;
        n->key = key; n->sock = s; n->last = std::chrono::steady_clock::now();
        node* dead = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            uint32_t same = 0;
            node* prev = nullptr; node* oldest = nullptr; node* oldest_prev = nullptr;
            for (node* c = m_head; c; prev = c, c = c->next) {
                if (c->key == key) { ++same; oldest = c; oldest_prev = prev; }
            }
            if (same >= max_per_host && oldest) {
                if (oldest_prev) oldest_prev->next = oldest->next; else m_head = oldest->next;
                --m_count; oldest->next = dead; dead = oldest;
            }
            if (m_count >= MAX_TOTAL) {               // global cap: evict tail
                node* p2 = nullptr; node* t = m_head;
                while (t && t->next) { p2 = t; t = t->next; }
                if (t) {
                    if (p2) p2->next = nullptr; else m_head = nullptr;
                    --m_count; t->next = dead; dead = t;
                }
            }
            n->next = m_head; m_head = n; ++m_count;
        }
        destroy_list(dead);
        return true;
    }

    void clear() noexcept {
        node* h;
        { std::lock_guard<std::mutex> lk(m_mutex); h = m_head; m_head = nullptr; m_count = 0; }
        destroy_list(h);
    }

    uint32_t idle_count() const noexcept {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_count;
    }

private:
    struct node {
        knst_c16string key;
        knst_tcp_socket* sock = nullptr;
        std::chrono::steady_clock::time_point last{};
        node* next = nullptr;
    };

    // Idle socket must show neither EOF/error/data on the wire nor buffered
    // TLS plaintext (a pooled connection with unread bytes is desynchronised).
    static bool alive(knst_tcp_socket* s) noexcept {
        if (!knst_net_detail::socket_looks_alive(s->native_handle())) return false;
        return !s->has_pending_input();
    }

    static void destroy_list(node* h) noexcept {
        while (h) { node* n = h->next; delete h->sock; delete h; h = n; }
    }
    static constexpr uint32_t MAX_TOTAL = 64;

    mutable std::mutex m_mutex;
    node*    m_head  = nullptr;
    uint32_t m_count = 0;
};


/* ============================================================================
 * 17) knst_http_client — HTTP/1.1 + HTTPS client
 * ============================================================================ */

class knst_http_client {
public:
    // Streaming download: called with decoded-as-received (identity) body chunks.
    // Return false to abort (request fails with CallbackAborted).
    typedef bool (*body_callback_t)(const uint8_t* data, size_t len, void* user);

    // Case-insensitive compare helper (same as knst_net_detail::ieq).
    // Exposed as a static member so the static multipart helpers below can
    // call it unqualified. Mirrors knst_http_request::ieq / knst_http_response::ieq.
    static bool ieq(const knst_c16string& a, const knst_c16string& b) noexcept {
        return knst_net_detail::ieq(a, b);
    }

    struct options {
        uint32_t timeout_ms       = 10000;      // per-operation timeout (0 = none)
        int      max_redirects    = 5;
        bool     follow_redirects = true;
        bool     auto_cookie      = true;
        knst_c16string basic_user;              // sent to the ORIGINAL origin only
        knst_c16string basic_pass;
        knst_c16string user_agent = u"KernelNucleusT/1.0";
        knst_c16string default_content_type = u"application/octet-stream";
        uint64_t max_response_bytes = 64ull * 1024 * 1024;   // on the wire
        uint32_t max_header_bytes   = 64 * 1024;
        uint32_t total_timeout_ms   = 120000;   // whole request incl. redirects (slow-drip guard); 0 = per-op only
        uint32_t dns_timeout_ms     = 0;        // 0 = use timeout_ms

        // Content-Encoding
        bool     allow_content_encoding   = true;                // send Accept-Encoding + decode
        uint64_t max_decompressed_bytes   = 256ull * 1024 * 1024;

        // Keep-alive
        bool     use_keepalive      = true;
        uint32_t keepalive_idle_ms  = 60000;
        uint32_t max_per_host       = 8;

        // DNS cache
        bool     dns_cache          = true;
        uint32_t dns_cache_ttl_ms   = 60000;

        // Proxy (http://[user:pass@]host:port). NO_PROXY: comma list.
        knst_c16string proxy_url;
        knst_c16string no_proxy;                 // empty => NO_PROXY/no_proxy env
        bool     use_env_proxy      = false;     // read HTTP(S)_PROXY from env

        // SSRF guard. false => connections to loopback / private / link-local /
        // CGNAT / ULA / multicast / reserved addresses are refused with
        // ForbiddenAddress. The check runs on the addresses actually connected
        // to (so it also covers DNS rebinding and every redirect hop). With a
        // proxy only IP-literal targets can be checked (the proxy resolves
        // names itself).
                // Source IP for outgoing connections. "" = default routing.
        // "192.168.1.5" or "fe80::1" = bind the client socket to that local
        // address before connecting (Wi-Fi / Ethernet / VPN split routing).
        // Invalid IPs fail the request with InvalidArgument; family mismatch
        // is skipped so mixed v4/v6 endpoint lists still work.
        knst_c16string bind_local_ip;

        bool     allow_private_networks = true;

        // A response without Content-Length/chunking is delimited by connection
        // close. A TCP reset / TLS truncation (no close_notify) in that case
        // normally FAILS the request (the body may be incomplete). Set true to
        // accept it as end-of-body (legacy-server compatibility).
        bool     lenient_close_delimited = false;
        // Chunked request upload: when true, the body is sent with
        // Transfer-Encoding: chunked instead of Content-Length. Useful for
        // streaming uploads whose size is not known upfront.
        bool     use_chunked_upload = false;

        // Streaming: if set, the (non-redirect) response body is delivered to the
        // callback in chunks instead of being buffered; response.body stays empty.
        // Content-Encoding is not requested in this mode.
        body_callback_t on_body      = nullptr;
        void*           on_body_user = nullptr;

        knst_vector<knst_http_header> headers;   // per-request extra headers
    };

    knst_http_client() noexcept = default;
    knst_http_client(const knst_http_client&) = delete;
    knst_http_client& operator=(const knst_http_client&) = delete;

    void clear_cookies() {
        std::lock_guard<std::mutex> lk(m_cookie_mutex);
        m_cookies.clear();
    }
    // Snapshot copy (thread-safe). Cheaper to iterate than to hold the lock.
    knst_vector<knst_cookie> cookies() const {
        std::lock_guard<std::mutex> lk(m_cookie_mutex);
        return m_cookies;
    }

    bool set_header(const knst_c16string& name, const knst_c16string& value) {
        return m_extra_headers.set_header(name, value);
    }
    void clear_headers() { m_extra_headers.headers.clear(); }

    void clear_connection_pool() noexcept { m_pool.clear(); }
    uint32_t idle_connections() const noexcept { return m_pool.idle_count(); }
    uint64_t connections_opened() const noexcept { return m_connects; }

    bool get (const knst_c16string& url, const options& o,
              knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"GET", url, nullptr, 0, o, out, err);
    }
    bool post(const knst_c16string& url, const knst_byte_string& body,
              const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"POST", url, body.data(), body.length(), o, out, err);
    }
    bool put (const knst_c16string& url, const knst_byte_string& body,
              const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"PUT", url, body.data(), body.length(), o, out, err);
    }
    bool del (const knst_c16string& url, const options& o,
              knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"DELETE", url, nullptr, 0, o, out, err);
    }
    bool head(const knst_c16string& url, const options& o,
              knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"HEAD", url, nullptr, 0, o, out, err);
    }
    bool request(const knst_c16string& method, const knst_c16string& url,
                 const knst_byte_string& body, const options& o,
                 knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(method, url, body.data(), body.length(), o, out, err);
    }

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool get_json(const knst_c16string& url, const options& o, knst_json& out,
                  knst_network_error& err) noexcept {
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

    // Opens a raw socket for a non-HTTP protocol that starts with an HTTP
    // Upgrade handshake (WebSocket, etc.). Honors proxy / DNS / TLS /
    // bind_local / SSRF options. Returns nullptr on failure; caller owns the
    // returned socket. Scheme must be "http" or "https" (ws/wss => http/https).
        // Opens a raw socket for an HTTP-Upgrade-based protocol (WebSocket, h2c, ...).
    // Raw protocols need a full-duplex byte stream AFTER the handshake, so
    // through an HTTP proxy we ALWAYS establish a CONNECT tunnel — even for
    // the plaintext `ws://` case. Without the tunnel the proxy would receive
    // a relative-URI GET request and could not route it to the target.
    knst_tcp_socket* open_raw_socket(const knst_url& u, const options& o,
                                     knst_network_error& err) noexcept {
        err = knst_network_error::None;
        proxy_cfg pc;
        if (!resolve_proxy(u, o, pc, err)) return nullptr;
        if (!o.allow_private_networks && pc.enabled && knst_net_detail::looks_like_ip_literal(u.host)) {
            knst_network_error e2 = knst_network_error::None;
            knst_ip_address ip = knst_ip_address::from_string(u.host, e2);
            if (e2 != knst_network_error::None || ip.is_non_public()) {
                err = knst_network_error::ForbiddenAddress; return nullptr;
            }
        }
        uint32_t req_timeout = o.timeout_ms;
        // Force a CONNECT tunnel regardless of scheme so the proxy never sees
        // a relative-URI request. For wss:// this is already the behavior; for
        // ws:// this is the fix. The tunnel is transparent to the WebSocket
        // handshake that follows.
        knst_c16string key = make_pool_key(u, pc, !o.allow_private_networks, o.bind_local_ip);
        bool reused = false;
        return open_connection(u, pc, o, req_timeout, /*allow_reuse=*/false, key, reused, err,
                               /*force_tunnel=*/true);
    }

    struct multipart_part {
        knst_c16string name, filename, content_type;
        knst_byte_string data;
        // Populated by parse_multipart() (empty when building).
        knst_vector<knst_http_header> headers;
    };

    static void build_multipart(const knst_vector<multipart_part>& parts,
                                knst_byte_string& out, knst_c16string& boundary_out) {
        static std::atomic<uint32_t> s_counter{0};
        uint32_t rnd = s_counter.fetch_add(1) ^
            static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        char bbuf[48];
        ::snprintf(bbuf, sizeof(bbuf), "----knst%08x%08x", rnd, rnd * 2654435761u);
        boundary_out = knst_c16string();
        for (const char* p = bbuf; *p; ++p) boundary_out.append(static_cast<char16_t>(*p));

        out.clear();
        auto ascii = [&](const char* s) { for (; *s; ++s) out.push_back(static_cast<uint8_t>(*s)); };
        auto esc = [&](const knst_c16string& s) {
            for (uint32_t i = 0; i < s.length(); ++i) {
                char16_t c = s[i];
                if (c == u'"' || c == u'\\') {
                    out.push_back('\\'); out.push_back(static_cast<uint8_t>(c));
                } else if (c == u'\r' || c == u'\n') {
                    out.push_back(' ');
                } else {
                    knst_c16string one(1, c);
                    knst_byte_string u = knst_net_detail::to_utf8(one);
                    for (uint32_t k = 0; k < u.length(); ++k) out.push_back(u[k]);
                }
            }
        };
        for (uint32_t i = 0; i < parts.size(); ++i) {
            ascii("--");
            for (uint32_t k = 0; k < boundary_out.length(); ++k) out.push_back(static_cast<uint8_t>(boundary_out[k]));
            ascii("\r\n");
            ascii("Content-Disposition: form-data; name=\""); esc(parts[i].name); ascii("\"");
            if (!parts[i].filename.empty()) { ascii("; filename=\""); esc(parts[i].filename); ascii("\""); }
            ascii("\r\n");
            if (!parts[i].content_type.empty()) { ascii("Content-Type: "); esc(parts[i].content_type); ascii("\r\n"); }
            ascii("\r\n");
            for (uint32_t j = 0; j < parts[i].data.length(); ++j) out.push_back(parts[i].data[j]);
            ascii("\r\n");
        }
        ascii("--");
        for (uint32_t k = 0; k < boundary_out.length(); ++k) out.push_back(static_cast<uint8_t>(boundary_out[k]));
        ascii("--\r\n");
    }

    static knst_c16string multipart_content_type(const knst_c16string& boundary) {
        knst_c16string ct = u"multipart/form-data; boundary=";
        ct.append(boundary);
        return ct;
    }

        // Extracts a `; key=value` parameter from a header value. Case-insensitive key.
    // Supports optional double quotes around the value.
    static knst_c16string extract_header_param(const knst_c16string& value, const knst_c16string& key) {
        uint32_t i = 0;
        while (i < value.length()) {
            while (i < value.length() && value[i] != u';') ++i;
            if (i >= value.length()) break;
            ++i;   // skip ';'
            while (i < value.length() && (value[i] == u' ' || value[i] == u'\t')) ++i;
            uint32_t ks = i;
            while (i < value.length() && value[i] != u'=' && value[i] != u';') ++i;
            uint32_t ke = i;
            while (ke > ks && (value[ke - 1] == u' ' || value[ke - 1] == u'\t')) --ke;
            knst_c16string k = value.substr(ks, ke - ks);
            if (i < value.length() && value[i] == u'=') {
                ++i;
                bool quoted = (i < value.length() && value[i] == u'"');
                if (quoted) ++i;
                uint32_t vs = i;
                if (quoted) {
                    while (i < value.length() && value[i] != u'"') ++i;
                    knst_c16string v = value.substr(vs, i - vs);
                    if (i < value.length()) ++i;
                    if (ieq(k, key)) return v;
                } else {
                    while (i < value.length() && value[i] != u';') ++i;
                    uint32_t ve = i;
                    while (ve > vs && (value[ve - 1] == u' ' || value[ve - 1] == u'\t')) --ve;
                    if (ieq(k, key)) return value.substr(vs, ve - vs);
                }
            }
        }
        return knst_c16string();
    }

    // Parses a multipart/form-data (or multipart/mixed) response body.
    // `content_type_header` is the raw Content-Type value, e.g.
    //   "multipart/form-data; boundary=----knstabc123"
    // Each part's headers are captured in multipart_part::headers; name,
    // filename, content_type and data are also populated for convenience.
    static bool parse_multipart(const knst_byte_string& body,
                                const knst_c16string& content_type_header,
                                knst_vector<multipart_part>& out,
                                knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();

        knst_c16string boundary = extract_header_param(content_type_header, u"boundary");
        if (boundary.empty()) { err = knst_network_error::HttpMalformedBody; return false; }

        knst_c16string dash_boundary = u"--";
        dash_boundary.append(boundary);
        knst_byte_string dash8 = knst_net_detail::to_utf8(dash_boundary);

        auto find_bytes = [](const knst_byte_string& hay, uint32_t from,
                             const uint8_t* needle, uint32_t nlen) -> uint32_t {
            if (nlen == 0 || from >= hay.length()) return static_cast<uint32_t>(-1);
            const uint8_t* h = hay.data();
            for (uint32_t i = from; i + nlen <= hay.length(); ++i) {
                if (::memcmp(h + i, needle, nlen) == 0) return i;
            }
            return static_cast<uint32_t>(-1);
        };

        uint32_t pos = 0;
        const uint32_t NPOS = static_cast<uint32_t>(-1);
        for (;;) {
            uint32_t delim_at = find_bytes(body, pos, dash8.data(), dash8.length());
            if (delim_at == NPOS) break;
            uint32_t after = delim_at + dash8.length();
            // Closing delimiter: dash8 + "--"
            if (after + 2 <= body.length() && body[after] == '-' && body[after + 1] == '-') break;
            // Consume CRLF after delimiter
            if (after + 2 <= body.length() && body[after] == '\r' && body[after + 1] == '\n') after += 2;
            else if (after + 1 <= body.length() && body[after] == '\n') after += 1;

            multipart_part part;
            // Parse part headers until blank line
            uint32_t hpos = after;
            bool headers_done = false;
            while (hpos < body.length()) {
                uint32_t eol = hpos;
                while (eol + 1 < body.length() && !(body[eol] == '\r' && body[eol + 1] == '\n')) ++eol;
                if (eol + 1 >= body.length()) { err = knst_network_error::HttpMalformedBody; return false; }
                uint32_t line_len = eol - hpos;
                if (line_len == 0) { hpos = eol + 2; headers_done = true; break; }  // blank line
                // Split into name:value
                uint32_t colon = hpos;
                while (colon < eol && body[colon] != ':') ++colon;
                if (colon == eol) { err = knst_network_error::HttpMalformedBody; return false; }
                knst_http_header h;
                uint32_t ns = hpos, ne = colon;
                while (ne > ns && (body[ne - 1] == ' ' || body[ne - 1] == '\t')) --ne;
                for (uint32_t k = ns; k < ne; ++k) h.name.append(static_cast<char16_t>(body[k]));
                uint32_t vs = colon + 1;
                while (vs < eol && (body[vs] == ' ' || body[vs] == '\t')) ++vs;
                uint32_t ve = eol;
                while (ve > vs && (body[ve - 1] == ' ' || body[ve - 1] == '\t')) --ve;
                for (uint32_t k = vs; k < ve; ++k) h.value.append(static_cast<char16_t>(body[k]));
                part.headers.push_back(h);
                hpos = eol + 2;
            }
            if (!headers_done) { err = knst_network_error::HttpMalformedBody; return false; }

            // Extract convenience fields
            for (uint32_t i = 0; i < part.headers.size(); ++i) {
                if (ieq(part.headers[i].name, u"Content-Type")) {
                    part.content_type = part.headers[i].value;
                } else if (ieq(part.headers[i].name, u"Content-Disposition")) {
                    knst_c16string n = extract_header_param(part.headers[i].value, u"name");
                    knst_c16string f = extract_header_param(part.headers[i].value, u"filename");
                    if (!n.empty()) part.name = n;
                    if (!f.empty()) part.filename = f;
                }
            }

            // Find next boundary to know part length
            uint32_t next_delim = find_bytes(body, hpos, dash8.data(), dash8.length());
            if (next_delim == NPOS) { err = knst_network_error::HttpMalformedBody; return false; }
            // Body extends from hpos to next_delim, minus trailing CRLF
            uint32_t body_end = next_delim;
            if (body_end >= hpos + 2 && body[body_end - 2] == '\r' && body[body_end - 1] == '\n') {
                body_end -= 2;
            } else if (body_end >= hpos + 1 && body[body_end - 1] == '\n') {
                body_end -= 1;
            }
            part.data.resize(body_end - hpos);
            if (part.data.length() != body_end - hpos) {
                err = knst_network_error::OutOfMemory; return false;
            }
            if (body_end > hpos) ::memcpy(&part.data[0], body.data() + hpos, body_end - hpos);

            out.push_back(part);
            pos = next_delim;
        }
        if (out.empty()) { err = knst_network_error::HttpMalformedBody; return false; }
        return true;
    }

private:
    typedef knst_net_detail::deadline deadline_t;

    mutable std::mutex       m_cookie_mutex;
    knst_vector<knst_cookie> m_cookies;
    knst_http_request        m_extra_headers;
    knst_connection_pool     m_pool;
    uint64_t                 m_connects = 0;

    struct exchange_state {
        bool got_byte = false;     // any response byte received
        bool reusable = false;     // connection may go back to the pool
    };

    struct proxy_cfg {
        enum class kind_t : uint8_t { Http, Socks5 };
        bool enabled = false;
        kind_t kind = kind_t::Http;
        bool socks5_remote_dns = false;    // true = socks5h (proxy resolves names)
        knst_c16string host;
        uint16_t port = 0;
        bool has_auth = false;
        knst_c16string auth;               // HTTP: "Basic ..."
        knst_byte_string user_raw;         // SOCKS5: raw username
        knst_byte_string pass_raw;         // SOCKS5: raw password
    };

    // ------------------------------------------------------------------
    // Buffered reader (works on any stream socket via virtual recv)
    // ------------------------------------------------------------------
    class buffered_reader {
    public:
        buffered_reader(knst_stream_socket_base& s, const knst_byte_string& pre,
                        uint32_t io_ms, const deadline_t& total, exchange_state& st) noexcept
            : m_s(s), m_buf(pre), m_io_ms(io_ms), m_total(total), m_st(st) {}

        bool has_buffered() const noexcept { return m_pos < m_buf.length(); }

        uint32_t io_budget(knst_network_error& err, bool& ok) const noexcept {
            bool expired = false;
            uint32_t rem = m_total.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; ok = false; return 0; }
            uint32_t t = (m_io_ms == 0) ? knst_net_detail::KNST_NET_INFINITE : m_io_ms;
            if (rem < t) t = rem;
            ok = true;
            return t;
        }

        // Refill the internal buffer; sets eof when the peer closed.
        bool fill(bool& eof, knst_network_error& err) noexcept {
            eof = false;
            bool ok; uint32_t t = io_budget(err, ok);
            if (!ok) return false;
            m_buf.resize(8192);
            m_pos = 0;
            if (m_buf.length() < 8192) { m_buf.clear(); err = knst_network_error::OutOfMemory; return false; }
            size_t n = 0;
            if (!m_s.recv(&m_buf[0], 8192, n, t, err)) { m_buf.clear(); return false; }
            m_buf.resize(static_cast<uint32_t>(n));
            if (n == 0) eof = true; else m_st.got_byte = true;
            return true;
        }

        bool read_some(uint8_t* dst, size_t cap, size_t& got, knst_network_error& err) noexcept {
            got = 0;
            if (cap == 0) return true;
            if (m_pos < m_buf.length()) {
                size_t n = std::min<size_t>(cap, m_buf.length() - m_pos);
                ::memcpy(dst, m_buf.data() + m_pos, n);
                m_pos += static_cast<uint32_t>(n); got = n; return true;
            }
            if (cap >= 4096) {                       // large read: straight into caller memory
                bool ok; uint32_t t = io_budget(err, ok);
                if (!ok) return false;
                size_t n = 0;
                if (!m_s.recv(dst, cap, n, t, err)) return false;
                if (n) m_st.got_byte = true;
                got = n; return true;
            }
            bool eof;
            if (!fill(eof, err)) return false;
            if (eof) return true;
            size_t c = std::min<size_t>(cap, m_buf.length());
            ::memcpy(dst, m_buf.data(), c);
            m_pos = static_cast<uint32_t>(c); got = c; return true;
        }

        bool read_exact(uint8_t* dst, size_t n, knst_network_error& err) noexcept {
            while (n > 0) {
                size_t got = 0;
                if (!read_some(dst, n, got, err)) return false;
                if (got == 0) { err = knst_network_error::ConnectionClosed; return false; }
                dst += got; n -= got;
            }
            return true;
        }

        // Reads up to and excluding "\n" (and one trailing "\r"). memchr-based.
        bool read_line(knst_byte_string& line, size_t max_len, knst_network_error& err) noexcept {
            line.clear();
            for (;;) {
                if (m_pos >= m_buf.length()) {
                    bool eof;
                    if (!fill(eof, err)) return false;
                    if (eof) { err = knst_network_error::ConnectionClosed; return false; }
                }
                const uint8_t* p = m_buf.data() + m_pos;
                size_t avail = m_buf.length() - m_pos;
                const uint8_t* nl = static_cast<const uint8_t*>(::memchr(p, '\n', avail));
                size_t take = nl ? static_cast<size_t>(nl - p) : avail;
                if (static_cast<size_t>(line.length()) + take > max_len) {
                    err = knst_network_error::HttpMalformedBody; return false;
                }
                if (!knst_net_detail::append_bytes(line, p, take)) { err = knst_network_error::OutOfMemory; return false; }
                m_pos += static_cast<uint32_t>(take + (nl ? 1 : 0));
                if (nl) {
                    if (line.length() && line[line.length() - 1] == '\r') line.resize(line.length() - 1);
                    return true;
                }
            }
        }

    private:
        knst_stream_socket_base& m_s;
        knst_byte_string m_buf;
        uint32_t m_pos = 0;
        uint32_t m_io_ms;
        deadline_t m_total;
        exchange_state& m_st;
    };

    static bool read_headers(knst_stream_socket_base& sock, uint32_t io_ms,
                             const deadline_t& total_dl, uint32_t max_head,
                             const knst_byte_string& initial,
                             knst_byte_string& head, knst_byte_string& preloaded,
                             exchange_state& st, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        head = initial;
        preloaded.clear();

        uint32_t scanned_upto = 0;
        auto find_end = [&](uint32_t from, uint32_t& hdr_end) -> bool {
            uint32_t n = head.length();
            if (n < 4) return false;
            uint32_t start = (from >= 3) ? (from - 3) : 0;
            const uint8_t* b = head.data();
            for (uint32_t i = start; i + 3 < n; ++i) {
                const void* cr = ::memchr(b + i, '\r', n - 3 - i);     // skip quickly to next CR
                if (!cr) return false;
                i = static_cast<uint32_t>(static_cast<const uint8_t*>(cr) - b);
                if (b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') { hdr_end = i + 4; return true; }
            }
            return false;
        };

        for (;;) {
            uint32_t hend = 0;
            if (find_end(scanned_upto, hend)) {
                if (hend > max_head) { err = knst_network_error::HttpMalformedHeaders; return false; }
                if (!knst_net_detail::append_bytes(preloaded, head.data() + hend, head.length() - hend)) {
                    err = knst_network_error::OutOfMemory; return false;
                }
                head.resize(hend);
                return true;
            }
            scanned_upto = head.length();
            if (head.length() > max_head) { err = knst_network_error::HttpMalformedHeaders; return false; }

            bool expired = false;
            uint32_t rem = total_dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; return false; }
            uint32_t t = (io_ms == 0) ? knst_net_detail::KNST_NET_INFINITE : io_ms;
            if (rem < t) t = rem;

            const uint32_t STEP = 4096;                       // receive straight into `head`
            uint32_t old = head.length();
            head.resize(old + STEP);
            if (head.length() != old + STEP) { head.resize(old); err = knst_network_error::OutOfMemory; return false; }
            size_t got = 0;
            if (!sock.recv(&head[old], STEP, got, t, err)) { head.resize(old); return false; }
            head.resize(old + static_cast<uint32_t>(got));
            if (got == 0) { err = knst_network_error::HttpConnectionClosed; return false; }
            st.got_byte = true;
        }
    }

    // Body sink: buffered (zero-copy into the response) or streamed to a callback.
    // The 32 KiB callback staging buffer lives on the heap (keeps stacks small).
    class body_acc {
    public:
        body_acc(knst_byte_string& out, uint64_t limit, body_callback_t cb, void* user) noexcept
            : m_sink(out, limit > knst_net_detail::KNST_NET_MAX_LEN
                          ? knst_net_detail::KNST_NET_MAX_LEN : static_cast<size_t>(limit)),
              m_limit(limit), m_cb(cb), m_user(user) {
            if (m_cb) m_tmp.reset(new (std::nothrow) uint8_t[TMP_SZ]);
        }

        size_t   chunk_max() const noexcept { return m_cb ? TMP_SZ : 262144; }
        uint64_t remaining() const noexcept { return m_limit - m_total; }

        uint8_t* begin(size_t n, knst_network_error& err) noexcept {
            if (n > m_limit - m_total) { err = knst_network_error::ResponseTooLarge; return nullptr; }
            if (m_cb) {
                if (!m_tmp) { err = knst_network_error::OutOfMemory; return nullptr; }
                return m_tmp.get();
            }
            if (!m_sink.room(n)) { err = m_sink.error(); return nullptr; }
            return m_sink.at(m_sink.len);
        }
        bool commit(size_t n, knst_network_error& err) noexcept {
            m_total += n;
            if (m_cb) {
                if (!m_cb(m_tmp.get(), n, m_user)) { err = knst_network_error::CallbackAborted; return false; }
            } else {
                m_sink.len += n;
            }
            return true;
        }
        void finish() noexcept { if (!m_cb) m_sink.finish(); }

        // Known Content-Length: allocate once (capped; larger bodies grow geometrically)
        void hint(uint64_t n) noexcept {
            if (m_cb || n == 0 || n > m_limit) return;
            const uint64_t CAP = 64ull * 1024 * 1024;
            m_sink.room(static_cast<size_t>(n < CAP ? n : CAP));
        }

    private:
        static constexpr size_t TMP_SZ = 32768;
        knst_net_detail::byte_sink m_sink;
        uint64_t m_limit, m_total = 0;
        body_callback_t m_cb;
        void* m_user;
        std::unique_ptr<uint8_t[]> m_tmp;
    };

    static bool read_exact_to(buffered_reader& rd, body_acc& acc, uint64_t n, knst_network_error& err) noexcept {
        while (n > 0) {
            size_t step = static_cast<size_t>(n < acc.chunk_max() ? n : acc.chunk_max());
            uint8_t* p = acc.begin(step, err);
            if (!p) return false;
            if (!rd.read_exact(p, step, err)) return false;
            if (!acc.commit(step, err)) return false;
            n -= step;
        }
        return true;
    }

    static bool read_chunked_body(buffered_reader& rd, body_acc& acc, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        knst_byte_string line;
        for (;;) {
            if (!rd.read_line(line, 1024, err)) return false;
            uint64_t sz = 0; uint32_t digits = 0;
            for (uint32_t i = 0; i < line.length(); ++i) {
                uint8_t c = line[i]; uint32_t d;
                if      (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
                else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
                else if (c == ';' || c == ' ' || c == '\t') break;
                else { err = knst_network_error::HttpMalformedBody; return false; }
                if (++digits > 16) { err = knst_network_error::HttpMalformedBody; return false; }
                sz = (sz << 4) | d;
            }
            if (digits == 0) { err = knst_network_error::HttpMalformedBody; return false; }
            if (sz == 0) break;
            if (sz > acc.remaining()) { err = knst_network_error::ResponseTooLarge; return false; }
            if (!read_exact_to(rd, acc, sz, err)) return false;
            if (!rd.read_line(line, 2, err)) return false;
            if (!line.empty()) { err = knst_network_error::HttpMalformedBody; return false; }
        }
        for (int t = 0; t < 64; ++t) {                    // trailers
            if (!rd.read_line(line, 8192, err)) return false;
            if (line.empty()) return true;
        }
        err = knst_network_error::HttpMalformedBody;
        return false;
    }

    static bool read_body_n(buffered_reader& rd, uint64_t want, body_acc& acc, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        acc.hint(want);
        return read_exact_to(rd, acc, want, err);
    }

    // Reads until the peer closes. Success only on a CLEAN end of stream
    // (FIN / TLS close_notify); resets and TLS truncation are errors.
    static bool read_body_to_eof(buffered_reader& rd, body_acc& acc, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        for (;;) {
            uint64_t rem = acc.remaining();
            if (rem == 0) {                               // limit reached: only EOF is acceptable
                uint8_t d; size_t got = 0;
                if (!rd.read_some(&d, 1, got, err)) return false;
                if (got) { err = knst_network_error::ResponseTooLarge; return false; }
                return true;
            }
            size_t n = static_cast<size_t>(rem < 65536 ? rem : 65536);
            if (n > acc.chunk_max()) n = acc.chunk_max();
            uint8_t* p = acc.begin(n, err);
            if (!p) return false;
            size_t got = 0;
            if (!rd.read_some(p, n, got, err)) return false;
            if (got == 0) return true;
            if (!acc.commit(got, err)) return false;
        }
    }

    // ------------------------------------------------------------------
    // Cookies / misc helpers
    // ------------------------------------------------------------------
    static bool domain_match(const knst_c16string& host, const knst_c16string& domain) {
        if (host.length() < domain.length()) return false;
        uint32_t off = host.length() - domain.length();
        if (off > 0 && host[off - 1] != u'.') return false;
        for (uint32_t i = 0; i < domain.length(); ++i) {
            char16_t hc = host[off + i], dc = domain[i];
            if (hc >= u'A' && hc <= u'Z') hc = static_cast<char16_t>(hc - u'A' + u'a');
            if (dc >= u'A' && dc <= u'Z') dc = static_cast<char16_t>(dc - u'A' + u'a');
            if (hc != dc) return false;
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

    // RFC 6265 §5.1.4 default-path
    static knst_c16string default_cookie_path(const knst_c16string& p) {
        if (p.empty() || p[0] != u'/') return knst_c16string(u"/");
        uint32_t last = 0;
        for (uint32_t i = 0; i < p.length(); ++i) if (p[i] == u'/') last = i;
        if (last == 0) return knst_c16string(u"/");
        return p.substr(0, last);
    }

    static bool same_origin(const knst_url& a, const knst_url& b) {
        return a.scheme == b.scheme && a.host == b.host && a.effective_port() == b.effective_port();
    }

    static int64_t now_unix_seconds() {
        return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    static bool is_idempotent(const knst_c16string& m) {
        return m == u"GET" || m == u"HEAD" || m == u"PUT" || m == u"DELETE" || m == u"OPTIONS";
    }

    static int64_t parse_http_date(const knst_c16string& s) {
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
        int hour = 0, minute = 0, sec = 0;
        while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { hour = hour * 10 + (s[i] - u'0'); ++i; }
        if (i < s.length() && s[i] == u':') ++i;
        while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { minute = minute * 10 + (s[i] - u'0'); ++i; }
        if (i < s.length() && s[i] == u':') ++i;
        while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { sec = sec * 10 + (s[i] - u'0'); ++i; }
        if (year < 1970 || year > 9999 || day < 1 || day > 31) return 0;

        int y = year - (month <= 1 ? 1 : 0);
        int era = (y >= 0 ? y : y - 399) / 400;
        int yoe = y - era * 400;
        int doy = (153 * (month + (month > 1 ? -3 : 9)) + 2) / 5 + day - 1;
        int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
        return days * 86400 + hour * 3600 + minute * 60 + sec;
    }

    // caller must hold m_cookie_mutex
    void purge_expired_cookies(int64_t now) {
        for (uint32_t i = 0; i < m_cookies.size(); ) {
            if (m_cookies[i].expires_unix > 0 && m_cookies[i].expires_unix <= now) m_cookies.erase(i);
            else ++i;
        }
    }

    void collect_cookies(const knst_http_response& resp, const knst_url& origin) {
        std::lock_guard<std::mutex> lk(m_cookie_mutex);
        int64_t now = now_unix_seconds();
        purge_expired_cookies(now);                       // keeps the jar from filling with dead entries
        const bool origin_is_ip = knst_net_detail::looks_like_ip_literal(origin.host);
        for (uint32_t i = 0; i < resp.headers.size(); ++i) {
            if (!knst_http_response::ieq(resp.headers[i].name, u"set-cookie")) continue;
            const knst_c16string& v = resp.headers[i].value;
            if (v.empty()) continue;
            uint32_t eq = 0; bool has_eq = false;
            for (uint32_t k = 0; k < v.length(); ++k) if (v[k] == u'=') { eq = k; has_eq = true; break; }
            if (!has_eq || eq == 0) continue;

            knst_cookie c;
            uint32_t name_end = eq;
            while (name_end > 0 && (v[name_end - 1] == u' ' || v[name_end - 1] == u'\t')) --name_end;
            c.name = v.substr(0, name_end);
            uint32_t semi = v.length();
            for (uint32_t k = eq + 1; k < v.length(); ++k) if (v[k] == u';') { semi = k; break; }
            uint32_t vs = eq + 1;
            while (vs < semi && (v[vs] == u' ' || v[vs] == u'\t')) ++vs;
            uint32_t ve = semi;
            while (ve > vs && (v[ve - 1] == u' ' || v[ve - 1] == u'\t')) --ve;
            c.value = v.substr(vs, ve - vs);
            if (c.name.empty() || !knst_net_detail::valid_header_name(c.name)) continue;
            {   // value: no control chars / DEL (prevents Cookie-header injection)
                bool bad = false;
                for (uint32_t q = 0; q < c.value.length(); ++q) {
                    char16_t ch = c.value[q];
                    if ((ch < 0x20 && ch != u'\t') || ch == 0x7F || ch >= 0x80) { bad = true; break; }
                }
                if (bad) continue;
            }

            c.domain = origin.host;
            c.path = default_cookie_path(origin.path);
            c.host_only = true;

            int64_t max_age = -2;
            int64_t expires_abs = 0;
            bool reject = false;

            uint32_t k = semi;
            while (k < v.length() && !reject) {
                while (k < v.length() && (v[k] == u';' || v[k] == u' ' || v[k] == u'\t')) ++k;
                uint32_t ks = k;
                while (k < v.length() && v[k] != u'=' && v[k] != u';') ++k;
                uint32_t ke = k;
                while (ke > ks && (v[ke - 1] == u' ' || v[ke - 1] == u'\t')) --ke;
                knst_c16string key = v.substr(ks, ke - ks);
                knst_c16string val;
                if (k < v.length() && v[k] == u'=') {
                    ++k;
                    uint32_t vs2 = k;
                    while (k < v.length() && v[k] != u';') ++k;
                    uint32_t ve2 = k;
                    while (ve2 > vs2 && (v[ve2 - 1] == u' ' || v[ve2 - 1] == u'\t')) --ve2;
                    while (vs2 < ve2 && (v[vs2] == u' ' || v[vs2] == u'\t')) ++vs2;
                    val = v.substr(vs2, ve2 - vs2);
                }
                if (knst_http_request::ieq(key, u"Domain")) {
                    knst_c16string dom = val;
                    if (dom.length() > 0 && dom[0] == u'.') dom = dom.substr(1, dom.length() - 1);
                    dom = knst_net_detail::lower16(dom);
                    uint32_t dots = 0;
                    for (uint32_t j = 0; j < dom.length(); ++j) if (dom[j] == u'.') ++dots;
                    if (dom.empty() || !domain_match(origin.host, dom) || dots == 0) { reject = true; break; }
                    // For IP-literal origins a Domain attribute may only repeat the exact host.
                    if (origin_is_ip && dom != origin.host) { reject = true; break; }
                    c.domain = dom;
                    c.host_only = false;
                } else if (knst_http_request::ieq(key, u"Path")) {
                    if (val.length() && val[0] == u'/') c.path = val;
                } else if (knst_http_request::ieq(key, u"Secure")) {
                    c.secure = true;
                } else if (knst_http_request::ieq(key, u"Max-Age")) {
                    int64_t ma = 0; bool neg = false; uint32_t j = 0;
                    if (val.length() && val[0] == u'-') { neg = true; j = 1; }
                    for (; j < val.length(); ++j) {
                        char16_t cc = val[j];
                        if (cc < u'0' || cc > u'9') break;
                        if (ma < 100000000000LL) ma = ma * 10 + (cc - u'0');
                    }
                    max_age = neg ? -ma : ma;
                } else if (knst_http_request::ieq(key, u"Expires")) {
                    expires_abs = parse_http_date(val);
                }
            }
            if (reject) continue;
            // A Secure cookie must not be set over a plaintext connection (RFC 6265bis §5.7).
            if (c.secure && origin.scheme != u"https") continue;

            if (max_age != -2)        c.expires_unix = (max_age <= 0) ? (now - 1) : (now + max_age);
            else if (expires_abs > 0) c.expires_unix = expires_abs;
            else                      c.expires_unix = 0;

            if (c.name.length() + c.value.length() > 4096) continue;
            if (m_cookies.size() >= 3000) continue;

            bool replaced = false;
            for (uint32_t j = 0; j < m_cookies.size(); ++j) {
                if (m_cookies[j].name == c.name && m_cookies[j].domain == c.domain &&
                    m_cookies[j].path == c.path) {
                    if (c.expires_unix != 0 && c.expires_unix < now) m_cookies.erase(j);
                    else m_cookies[j] = c;
                    replaced = true; break;
                }
            }
            if (!replaced && (c.expires_unix == 0 || c.expires_unix > now)) m_cookies.push_back(c);
        }
    }

    // ------------------------------------------------------------------
    // Proxy configuration
    // ------------------------------------------------------------------
    static bool host_in_no_proxy(const knst_c16string& host, const knst_c16string& list) {
        uint32_t start = 0;
        for (uint32_t k = 0; k <= list.length(); ++k) {
            if (k < list.length() && list[k] != u',' && list[k] != u' ' && list[k] != u'\t') continue;
            knst_c16string tok = knst_net_detail::lower16(knst_net_detail::trim16(list.substr(start, k - start)));
            start = k + 1;
            if (tok.empty()) continue;
            if (tok.length() == 1 && tok[0] == u'*') return true;
            // strip :port (not for bare IPv6)
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

    static bool resolve_proxy(const knst_url& target, const options& o,
                              proxy_cfg& pc, knst_network_error& err) {
        pc = proxy_cfg{};
        err = knst_network_error::None;
        knst_c16string purl = o.proxy_url;
        char buf[2048];
        if (purl.empty() && o.use_env_proxy) {
#if defined(__ANDROID__)
            
            if (knst_net_detail::android_jni::get_system_proxy(buf, sizeof(buf))) {
                purl = knst_net_detail::ascii_to_c16(buf);
            }
#endif
            if (purl.empty()) {
                // For plain http only the lowercase variable is honoured: an uppercase
                // HTTP_PROXY can be injected by a CGI "Proxy:" request header (httpoxy).
                const bool https = target.scheme == u"https";
                const char* names[2] = { https ? "https_proxy" : "http_proxy", "HTTPS_PROXY" };
                const int n_names = https ? 2 : 1;
                for (int i = 0; i < n_names; ++i)
                    if (knst_net_detail::get_env(names[i], buf, sizeof(buf))) {
                        purl = knst_net_detail::ascii_to_c16(buf); break;
                    }
            }
        }
        if (purl.empty()) return true;

        knst_c16string np = o.no_proxy;
        if (np.empty()) {
            if (knst_net_detail::get_env("no_proxy", buf, sizeof(buf)) ||
                knst_net_detail::get_env("NO_PROXY", buf, sizeof(buf)))
                np = knst_net_detail::ascii_to_c16(buf);
        }
        if (!np.empty() && host_in_no_proxy(target.host, np)) return true;

        if (!knst_net_detail::has_scheme(purl)) {
            knst_c16string t = u"http://";
            t.append(purl);
            purl = t;
        }
                knst_url pu;
        if (!knst_url::parse(purl, pu, err)) { err = knst_network_error::ProxyFailed; return false; }

        if (pu.scheme == u"http") {
            pc.kind = proxy_cfg::kind_t::Http;
        } else if (pu.scheme == u"socks5") {
            pc.kind = proxy_cfg::kind_t::Socks5;
            pc.socks5_remote_dns = false;
        } else if (pu.scheme == u"socks5h") {
            pc.kind = proxy_cfg::kind_t::Socks5;
            pc.socks5_remote_dns = true;
        } else {
            err = knst_network_error::NotSupported; return false;
        }

        pc.enabled = true;
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
                knst_byte_string up_colon = up;
                up_colon.push_back(':');
                for (uint32_t i = 0; i < pw.length(); ++i) up_colon.push_back(pw[i]);
                pc.auth = u"Basic ";
                pc.auth.append(knst_net_detail::base64_encode_bytes(up_colon));
            } else {
                pc.user_raw = up;
                pc.pass_raw = pw;
            }
            pc.has_auth = true;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // DNS with literal fast-path + cache + async deadline
    // ------------------------------------------------------------------
    static bool resolve_host(const knst_c16string& host, uint16_t port, const options& o,
                             uint32_t dns_to, const deadline_t& dl,
                             knst_vector<knst_endpoint>& out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();
        if (o.dns_cache && knst_dns_cache::global().lookup(host, port, out)) return true;
        if (!knst_dns_resolver::resolve_async(host, port, out, err,
                                              knst_net_detail::dns_budget(dns_to, dl)))
            return false;
        if (o.dns_cache) knst_dns_cache::global().store(host, out, o.dns_cache_ttl_ms);
        return true;
    }

    // SSRF guard: drops non-public addresses; ForbiddenAddress if none remain.
    static bool filter_public(knst_vector<knst_endpoint>& list, knst_network_error& err) noexcept {
        knst_vector<knst_endpoint> keep;
        for (uint32_t i = 0; i < list.size(); ++i) {
            if (list[i].address.is_non_public()) continue;
            if (!keep.push_back(list[i])) { err = knst_network_error::OutOfMemory; return false; }
        }
        if (keep.size() == 0) { err = knst_network_error::ForbiddenAddress; return false; }
        list.clear();
        for (uint32_t i = 0; i < keep.size(); ++i)
            if (!list.push_back(keep[i])) { err = knst_network_error::OutOfMemory; return false; }
        return true;
    }

    // HTTP CONNECT tunnel over an already-connected TCP socket to the proxy.
    static bool proxy_connect_tunnel(knst_tcp_socket& sock, const knst_url& u,
                                     const proxy_cfg& pc, const options& o,
                                     const deadline_t& dl, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        knst_c16string hp = u.host_bracketed();
        hp.append(u':');
        knst_net_detail::append_uint(hp, u.effective_port());

        knst_http_request r;
        r.method = u"CONNECT";
        r.path = hp;
        if (!r.set_header_internal(u"Host", hp)) { err = knst_network_error::InvalidArgument; return false; }
        if (!o.user_agent.empty()) r.set_header_internal(u"User-Agent", o.user_agent);
        r.set_header_internal(u"Proxy-Connection", u"keep-alive");
        if (pc.has_auth) r.set_header_internal(u"Proxy-Authorization", pc.auth);
        knst_byte_string bytes = r.serialize();

        bool expired = false;
        uint32_t t = dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; return false; }
        if (!sock.send_all(bytes, t, err)) return false;

        knst_byte_string head, pre, initial;
        exchange_state st;
        t = dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; return false; }
        if (!read_headers(sock, t == knst_net_detail::KNST_NET_INFINITE ? 0 : t, dl, 16384, initial, head, pre, st, err))
            return false;
        knst_http_response pr;
        if (!knst_http_response::parse(head, pr, err)) { err = knst_network_error::ProxyFailed; return false; }
        if (pr.status < 200 || pr.status >= 300 || pre.length() != 0) {
            err = knst_network_error::ProxyFailed; return false;
        }
        return true;
    }

    // RFC 1928 SOCKS5 CONNECT handshake. Socket must be connected to the proxy.
    static bool socks5_handshake(knst_tcp_socket& sock, const knst_c16string& target_host,
                                 uint16_t target_port, const proxy_cfg& pc,
                                 const deadline_t& dl, knst_network_error& err) noexcept {
        err = knst_network_error::None;

        auto budget = [&](bool& ok) -> uint32_t {
            bool expired; uint32_t rem = dl.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; ok = false; return 0; }
            ok = true;
            return rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem;
        };

        // 1) Greeting: [0x05, nmethods, methods...]
        {
            uint8_t greet[4];
            int n = 0;
            greet[n++] = 0x05;
            if (pc.has_auth) {
                greet[n++] = 0x02;
                greet[n++] = 0x00;   // no-auth
                greet[n++] = 0x02;   // user/pass
            } else {
                greet[n++] = 0x01;
                greet[n++] = 0x00;
            }
            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.send_all(greet, static_cast<size_t>(n), t, err)) return false;
        }

        // 2) Method selection: [0x05, method]
        uint8_t method = 0;
        {
            uint8_t resp[2];
            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.recv_exact(resp, 2, t, err)) return false;
            if (resp[0] != 0x05) { err = knst_network_error::ProxyFailed; return false; }
            method = resp[1];
            if (method == 0xFF) { err = knst_network_error::ProxyFailed; return false; }
        }

        // 3) Auth (if server chose user/pass)
        if (method == 0x02) {
            if (!pc.has_auth || pc.user_raw.length() > 255 || pc.pass_raw.length() > 255) {
                err = knst_network_error::ProxyFailed; return false;
            }
            uint8_t buf[513];
            int n = 0;
            buf[n++] = 0x01;
            buf[n++] = static_cast<uint8_t>(pc.user_raw.length());
            for (uint32_t i = 0; i < pc.user_raw.length(); ++i) buf[n++] = pc.user_raw[i];
            buf[n++] = static_cast<uint8_t>(pc.pass_raw.length());
            for (uint32_t i = 0; i < pc.pass_raw.length(); ++i) buf[n++] = pc.pass_raw[i];

            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.send_all(buf, static_cast<size_t>(n), t, err)) return false;

            uint8_t resp[2];
            t = budget(ok); if (!ok) return false;
            if (!sock.recv_exact(resp, 2, t, err)) return false;
            if (resp[0] != 0x01 || resp[1] != 0x00) { err = knst_network_error::ProxyFailed; return false; }
        } else if (method != 0x00) {
            err = knst_network_error::ProxyFailed; return false;
        }

        // 4) CONNECT request
        {
            uint8_t addr[1 + 16];
            int addr_total = 0;
            knst_network_error ip_err = knst_network_error::None;
            knst_ip_address ip = knst_ip_address::from_string(target_host, ip_err);
            if (ip_err == knst_network_error::None && ip.is_valid()) {
                if (ip.is_ipv4()) {
                    addr[0] = 0x01;
                    uint8_t r4[4]; ip.to_raw_ipv4(r4);
                    ::memcpy(addr + 1, r4, 4); addr_total = 5;
                } else {
                    addr[0] = 0x04;
                    uint8_t r6[16] = {}; ip.to_raw_ipv6(r6);
                    ::memcpy(addr + 1, r6, 16); addr_total = 17;
                }
            } else {
                knst_byte_string h8 = knst_net_detail::to_utf8(target_host);
                if (h8.length() == 0 || h8.length() > 255) { err = knst_network_error::ProxyFailed; return false; }
                addr[0] = 0x03;
                addr[1] = static_cast<uint8_t>(h8.length());
                ::memcpy(addr + 2, h8.data(), h8.length());
                addr_total = 2 + static_cast<int>(h8.length());
            }

            uint8_t req[3 + 17 + 2];
            int n = 0;
            req[n++] = 0x05;
            req[n++] = 0x01;   // CONNECT
            req[n++] = 0x00;   // reserved
            for (int i = 0; i < addr_total; ++i) req[n++] = addr[i];
            req[n++] = static_cast<uint8_t>((target_port >> 8) & 0xFF);
            req[n++] = static_cast<uint8_t>(target_port & 0xFF);

            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.send_all(req, static_cast<size_t>(n), t, err)) return false;
        }

        // 5) Response: [0x05, rep, rsv, atyp, bind_addr, bind_port]
        {
            uint8_t resp[4];
            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.recv_exact(resp, 4, t, err)) return false;
            if (resp[0] != 0x05) { err = knst_network_error::ProxyFailed; return false; }
            if (resp[1] != 0x00) {
                err = (resp[1] == 0x05) ? knst_network_error::ConnectionRefused
                                        : knst_network_error::ProxyFailed;
                return false;
            }
            int consume = 0;
            if (resp[3] == 0x01) consume = 4;
            else if (resp[3] == 0x04) consume = 16;
            else if (resp[3] == 0x03) {
                uint8_t dlen = 0;
                t = budget(ok); if (!ok) return false;
                if (!sock.recv_exact(&dlen, 1, t, err)) return false;
                consume = static_cast<int>(dlen);
            } else { err = knst_network_error::ProxyFailed; return false; }
            consume += 2;   // trailing port
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

    // Returns a connected socket (new or pooled). Ownership to caller.
    knst_tcp_socket* open_connection(const knst_url& u, const proxy_cfg& pc, const options& o,
                                     uint32_t req_timeout, bool allow_reuse,
                                     const knst_c16string& key, bool& reused,
                                     knst_network_error& err,
                                     bool force_tunnel = false) noexcept {
        reused = false;
        err = knst_network_error::None;

        // Resolve the source IP once (used by every connect path below).
        knst_ip_address bind_ip{};
        bool have_bind = false;
        if (!o.bind_local_ip.empty()) {
            knst_network_error e2 = knst_network_error::None;
            bind_ip = knst_ip_address::from_string(o.bind_local_ip, e2);
            if (e2 != knst_network_error::None || !bind_ip.is_valid()) {
                err = knst_network_error::InvalidArgument;
                return nullptr;
            }
            have_bind = true;
        }
        auto apply_bind = [&](knst_tcp_socket* s) -> bool {
            if (!have_bind) return true;
            knst_network_error be = knst_network_error::None;
            if (!s->bind_local(knst_endpoint(bind_ip, 0), be)) { err = be; return false; }
            return true;
        };

        if (allow_reuse && o.use_keepalive) {
            knst_tcp_socket* s = m_pool.take(key, o.keepalive_idle_ms);
            if (s) {
                s->set_read_timeout(req_timeout);
                s->set_write_timeout(req_timeout);
                reused = true;
                return s;
            }
        }
        // With a proxy the destination name is resolved by the proxy: only an
        // IP-literal target can be vetted here.
        if (!o.allow_private_networks && pc.enabled && knst_net_detail::looks_like_ip_literal(u.host)) {
            knst_network_error e2 = knst_network_error::None;
            knst_ip_address ip = knst_ip_address::from_string(u.host, e2);
            if (e2 != knst_network_error::None || ip.is_non_public()) {
                err = knst_network_error::ForbiddenAddress; return nullptr;
            }
        }

        auto conn_dl = deadline_t::after(req_timeout == 0 ? knst_net_detail::KNST_NET_INFINITE : req_timeout);
        const knst_c16string& chost = pc.enabled ? pc.host : u.host;
        uint16_t cport = pc.enabled ? pc.port : u.effective_port();
        uint32_t dns_to = o.dns_timeout_ms == 0 ? req_timeout : o.dns_timeout_ms;

        knst_vector<knst_endpoint> list;
        if (!resolve_host(chost, cport, o, dns_to, conn_dl, list, err)) return nullptr;
        if (!o.allow_private_networks && !pc.enabled && !filter_public(list, err)) return nullptr;

        // socks5 (not socks5h): resolve the TARGET locally, hand the IP to the proxy.
        knst_c16string socks_host = u.host;
        if (pc.enabled && pc.kind == proxy_cfg::kind_t::Socks5 && !pc.socks5_remote_dns &&
            !knst_net_detail::looks_like_ip_literal(u.host)) {
            knst_vector<knst_endpoint> tl;
            if (!resolve_host(u.host, u.effective_port(), o, dns_to, conn_dl, tl, err)) return nullptr;
            if (!o.allow_private_networks && !filter_public(tl, err)) return nullptr;
            socks_host = tl[0].address.to_string();
        }

        bool expired = false;
        uint32_t rem = conn_dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; return nullptr; }
        uint32_t conn_to = rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem;

        if (u.scheme == u"http" && !(pc.enabled && pc.kind == proxy_cfg::kind_t::Http && force_tunnel)) {
            knst_tcp_socket* s = new (std::nothrow) knst_tcp_socket();
            if (!s) { err = knst_network_error::OutOfMemory; return nullptr; }
            s->set_read_timeout(req_timeout);
            s->set_write_timeout(req_timeout);
            if (!apply_bind(s)) { delete s; return nullptr; }
            if (!s->connect_any(list, conn_to, err)) { delete s; return nullptr; }
            if (pc.enabled && pc.kind == proxy_cfg::kind_t::Socks5) {
                if (!socks5_handshake(*s, socks_host, u.effective_port(), pc, conn_dl, err)) {
                    delete s; return nullptr;
                }
            }
            ++m_connects;
            return s;
        }
        // Plain HTTP through an HTTP proxy with force_tunnel: fall through to
        // the CONNECT-tunnel path (needed for WebSocket etc.). If not for
        // force_tunnel, this would be a plain HTTP proxy request and the
        // absolute-URI form in do_request() would take over.
        if (u.scheme == u"http" && pc.enabled && pc.kind == proxy_cfg::kind_t::Http && force_tunnel) {
            knst_tcp_socket* s = new (std::nothrow) knst_tcp_socket();
            if (!s) { err = knst_network_error::OutOfMemory; return nullptr; }
            s->set_read_timeout(req_timeout);
            s->set_write_timeout(req_timeout);
            if (!apply_bind(s)) { delete s; return nullptr; }
            if (!s->connect_any(list, conn_to, err)) { delete s; return nullptr; }
            bool ok = proxy_connect_tunnel(*s, u, pc, o, conn_dl, err);
            if (!ok) { delete s; return nullptr; }
            ++m_connects;
            return s;
        }
    #ifdef KNST_NETWORK_ENABLE_TLS
        knst_ssl_socket* s = new (std::nothrow) knst_ssl_socket();
        if (!s) { err = knst_network_error::OutOfMemory; return nullptr; }
        s->set_read_timeout(req_timeout);
        s->set_write_timeout(req_timeout);
        if (!apply_bind(s)) { delete s; return nullptr; }
        bool ok;
        if (!pc.enabled) {
            ok = s->connect_tls_any(list, u.host, conn_to, err);
        } else {
            ok = s->connect_any(list, conn_to, err);
            if (ok) {
                if (pc.kind == proxy_cfg::kind_t::Http) {
                    ok = proxy_connect_tunnel(*s, u, pc, o, conn_dl, err);
                } else {
                                        ok = socks5_handshake(*s, socks_host, u.effective_port(), pc, conn_dl, err);
                }
            }
            if (ok) {
                rem = conn_dl.remaining_ms(expired);
                if (expired) { err = knst_network_error::Timeout; ok = false; }
                else ok = s->tls_handshake(u.host,
                                           rem == knst_net_detail::KNST_NET_INFINITE ? 0 : rem,
                                           err, u.effective_port());
            }
        }
        if (!ok) { delete s; return nullptr; }
        ++m_connects;
        return s;
    #else
        err = knst_network_error::TlsNotSupported;
        return nullptr;
    #endif
    }

    // `strict` (SSRF guard on) is part of the key: a connection made without
    // the guard must never be reused by a guarded request.
    static knst_c16string make_pool_key(const knst_url& u, const proxy_cfg& pc,
                                         bool strict, const knst_c16string& bind_ip) {
        knst_c16string k;
        k.append(strict ? u"S|" : u"O|");
        if (!bind_ip.empty()) { k.append(u"B|"); k.append(bind_ip); k.append(u'|'); }
        if (pc.enabled && u.scheme == u"http") {
            k.append(u"P|"); k.append(pc.host); k.append(u':'); knst_net_detail::append_uint(k, pc.port);
        } else {
            k.append(pc.enabled ? u"T|" : u"D|");
            k.append(u.scheme); k.append(u"://"); k.append(u.host); k.append(u':');
            knst_net_detail::append_uint(k, u.effective_port());
            if (pc.enabled) { k.append(u'|'); k.append(pc.host); k.append(u':'); knst_net_detail::append_uint(k, pc.port); }
        }
        return k;
    }

    // ------------------------------------------------------------------
    // One request/response exchange on a connected socket
    // `body` is sent straight from the caller's memory (no copy).
    // ------------------------------------------------------------------
    static bool do_exchange(knst_stream_socket_base& sock, const knst_http_request& req,
                            const uint8_t* body, size_t body_len,
                            const options& o, uint32_t req_timeout, const deadline_t& total_dl,
                            knst_http_response& resp, exchange_state& st,
                            knst_network_error& err) noexcept {
        err = knst_network_error::None;
        st = exchange_state{};

        {   // send: head (+ small body in the same packet), large body directly
            knst_c16string te_req = req.get_header(u"Transfer-Encoding");
            const bool chunked_upload = knst_http_request::ieq(knst_net_detail::trim16(te_req), u"chunked");
            if (chunked_upload) {
                knst_byte_string hb = req.serialize_head();
                if (!sock.send_all(hb, req_timeout, err)) return false;
                if (body_len > 0) {
                    char sz[32];
                    int n = ::snprintf(sz, sizeof(sz), "%zx\r\n", body_len);
                    if (!sock.send_all(sz, static_cast<size_t>(n), req_timeout, err)) return false;
                    if (!sock.send_all(body, body_len, req_timeout, err)) return false;
                    if (!sock.send_all("\r\n", 2, req_timeout, err)) return false;
                }
                if (!sock.send_all("0\r\n\r\n", 5, req_timeout, err)) return false;
                body_len = 0;
            } else {
                knst_byte_string head_bytes = req.serialize_head();
                if (body_len > 0 && body_len <= 16384) {
                    if (!knst_net_detail::append_bytes(head_bytes, body, body_len)) { err = knst_network_error::OutOfMemory; return false; }
                    body_len = 0;
                }
                if (!sock.send_all(head_bytes, req_timeout, err)) return false;
                if (body_len > 0 && !sock.send_all(body, body_len, req_timeout, err)) return false;
            }
        }

        knst_byte_string preloaded, head, initial;
        for (int interim = 0; ; ++interim) {
            if (!read_headers(sock, req_timeout, total_dl, o.max_header_bytes,
                              initial, head, preloaded, st, err)) return false;
            if (!knst_http_response::parse(head, resp, err)) return false;
            if (resp.status >= 100 && resp.status < 200 && resp.status != 101) {
                if (interim >= 8) { err = knst_network_error::HttpMalformedStatus; return false; }
                resp = knst_http_response{};
                initial = preloaded;
                preloaded.clear();
                continue;
            }
            break;
        }

        knst_c16string te = resp.get_header(u"Transfer-Encoding");
        knst_c16string cl = resp.get_header(u"Content-Length");
        knst_c16string ce = resp.get_header(u"Content-Encoding");

        const bool is_head = (req.method == u"HEAD");
        const bool no_body = is_head || (resp.status >= 100 && resp.status < 200) ||
                             resp.status == 204 || resp.status == 304;
        const bool has_encoding = !ce.empty() && !knst_http_request::ieq(knst_net_detail::trim16(ce), u"identity");
        const bool will_redirect = resp.is_redirect() && o.follow_redirects &&
                                   !resp.get_header(u"Location").empty();
        const bool stream = o.on_body != nullptr && !will_redirect && !no_body;

        if (!no_body && has_encoding && (!o.allow_content_encoding || stream)) {
            err = knst_network_error::HttpUnsupportedEncoding;
            return false;
        }

        uint64_t eff_cap = will_redirect ? 65536ull : o.max_response_bytes;
        buffered_reader rd(sock, preloaded, req_timeout, total_dl, st);
        body_acc acc(resp.body, eff_cap, stream ? o.on_body : nullptr, o.on_body_user);
        bool framed = true;                 // body length known => connection reusable

        if (no_body) {
            resp.body.clear();
        } else if (!te.empty()) {
            bool chunked = false;
            uint32_t start = 0;
            for (uint32_t k = 0; k <= te.length(); ++k) {
                if (k == te.length() || te[k] == u',') {
                    knst_c16string tok = knst_net_detail::trim16(te.substr(start, k - start));
                    start = k + 1;
                    if (knst_http_request::ieq(tok, u"chunked")) chunked = true;
                    else if (tok.length() > 0) { err = knst_network_error::HttpUnsupportedTransfer; return false; }
                }
            }
            if (!chunked) { err = knst_network_error::HttpUnsupportedTransfer; return false; }
            if (!read_chunked_body(rd, acc, err)) {
                if (!(will_redirect && err == knst_network_error::ResponseTooLarge)) return false;
                resp.body.clear(); err = knst_network_error::None; framed = false;
            } else acc.finish();
        } else if (!cl.empty()) {
            uint64_t want = 0;
            for (uint32_t k = 0; k < cl.length(); ++k) {
                if (cl[k] < u'0' || cl[k] > u'9') { err = knst_network_error::HttpMalformedHeaders; return false; }
                uint64_t dgt = static_cast<uint64_t>(cl[k] - u'0');
                if (want > (UINT64_MAX - dgt) / 10) { err = knst_network_error::ResponseTooLarge; return false; }
                want = want * 10 + dgt;
            }
            if (want > eff_cap) {
                if (!will_redirect) { err = knst_network_error::ResponseTooLarge; return false; }
                resp.body.clear(); framed = false;       // ignore oversized redirect body
            } else {
                if (!read_body_n(rd, want, acc, err)) return false;
                acc.finish();
            }
        } else {
            framed = false;                              // read until close
            bool dropped = false;
            bool ok = read_body_to_eof(rd, acc, err);
            if (!ok) {
                if (o.lenient_close_delimited &&
                    (err == knst_network_error::ConnectionReset ||
                     err == knst_network_error::ConnectionClosed)) {
                    err = knst_network_error::None; ok = true;
                } else if (will_redirect && err == knst_network_error::ResponseTooLarge) {
                    resp.body.clear(); err = knst_network_error::None; ok = true; dropped = true;
                }
            }
            if (!ok) return false;
            if (!dropped) acc.finish();
        }

        // Content-Encoding decode (buffered mode only; not for dropped redirect bodies)
        if (!no_body && has_encoding && !stream && !will_redirect && resp.body.length() > 0) {
            size_t lim = o.max_decompressed_bytes > knst_net_detail::KNST_NET_MAX_LEN
                       ? knst_net_detail::KNST_NET_MAX_LEN : static_cast<size_t>(o.max_decompressed_bytes);
            if (!knst_net_detail::decode_content_encoding(ce, resp.body, lim, err)) return false;
            for (uint32_t i = 0; i < resp.headers.size(); ) {
                if (knst_http_response::ieq(resp.headers[i].name, u"Content-Encoding")) resp.headers.erase(i);
                else ++i;
            }
            for (uint32_t i = 0; i < resp.headers.size(); ++i) {
                if (knst_http_response::ieq(resp.headers[i].name, u"Content-Length")) {
                    knst_c16string nv; knst_net_detail::append_uint(nv, resp.body.length());
                    resp.headers[i].value = nv;
                }
            }
        }

        // Reusability for keep-alive
        const bool close_hdr = resp.header_has_token(u"Connection", u"close");
        const bool ka_hdr    = resp.header_has_token(u"Connection", u"keep-alive");
        const bool http10    = (resp.version == u"HTTP/1.0");
        st.reusable = o.use_keepalive && framed && !close_hdr && (!http10 || ka_hdr) &&
                      resp.status != 101 && !rd.has_buffered();
        return true;
    }

    // ------------------------------------------------------------------
    // Main request handler: redirects, auth, cookies, proxy, pool, retry
    // ------------------------------------------------------------------
    bool do_request(const knst_c16string& method, const knst_c16string& url,
                    const void* body, size_t body_len, const options& o,
                    knst_http_response& out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out = knst_http_response{};

        if (!knst_net_detail::valid_header_name(method)) { err = knst_network_error::InvalidArgument; return false; }

        knst_c16string cur_url = url;
        int hops = 0;
        knst_c16string cur_method = method;
        const uint8_t* body_ptr = static_cast<const uint8_t*>(body);   // caller memory, never copied
        size_t body_n = (body && body_len > 0) ? body_len : 0;
        if (!body_ptr) body_n = 0;
        if (body_n > 0xFFFFFFFFu) { err = knst_network_error::InvalidArgument; return false; }

        knst_url origin_url;
        bool origin_set = false;

        auto total_dl = deadline_t::after(o.total_timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE
                                                                  : o.total_timeout_ms);
        for (;;) {
            knst_url u;
            if (!knst_url::parse(cur_url, u, err)) {
                if (err == knst_network_error::None) err = knst_network_error::HttpParseUrl;
                return false;
            }
            if (!origin_set) { origin_url = u; origin_set = true; }
            if (!u.userinfo.empty()) u.userinfo.clear();       // auth only via options

            if (u.scheme != u"http" && u.scheme != u"https") { err = knst_network_error::NotSupported; return false; }

            uint32_t req_timeout = o.timeout_ms;
            if (!total_dl.infinite) {
                bool expired; uint32_t rem = total_dl.remaining_ms(expired);
                if (expired) { err = knst_network_error::Timeout; return false; }
                if (req_timeout == 0 || rem < req_timeout) req_timeout = rem;
            }

            proxy_cfg pc;
            if (!resolve_proxy(u, o, pc, err)) return false;
            const bool tunnel   = pc.enabled && u.scheme == u"https";
            const bool abs_form = pc.enabled && !tunnel;

            // ---- build request ----
            knst_http_request req;
            req.method = cur_method;
            if (abs_form) {
                knst_c16string abs_t = u.scheme;
                abs_t.append(u"://");
                abs_t.append(u.host_header_value());
                abs_t.append(u.request_target());
                req.path = abs_t;
            } else {
                req.path = u.request_target();
            }
            req.set_header_internal(u"Host", u.host_header_value());
            if (!o.user_agent.empty()) req.set_header_internal(u"User-Agent", o.user_agent);
            req.set_header_internal(u"Accept", u"*/*");
            req.set_header_internal(u"Accept-Encoding",
                (o.allow_content_encoding && !o.on_body) ? u"gzip, deflate, identity" : u"identity");
            req.set_header_internal(u"Connection", o.use_keepalive ? u"keep-alive" : u"close");
            if (abs_form && pc.has_auth) req.set_header_internal(u"Proxy-Authorization", pc.auth);

            const bool cross_origin = !same_origin(u, origin_url);
            auto is_sensitive = [](const knst_c16string& name) {
                return knst_http_request::ieq(name, u"Authorization") ||
                       knst_http_request::ieq(name, u"Proxy-Authorization") ||
                       knst_http_request::ieq(name, u"Cookie") ||
                       knst_http_request::ieq(name, u"X-Api-Key") ||
                       knst_http_request::ieq(name, u"X-Auth-Token");
            };
            for (uint32_t i = 0; i < o.headers.size(); ++i) {
                if (cross_origin && is_sensitive(o.headers[i].name)) continue;
                if (!req.set_header(o.headers[i].name, o.headers[i].value)) { err = knst_network_error::HttpInvalidHeader; return false; }
            }
            for (uint32_t i = 0; i < m_extra_headers.headers.size(); ++i) {
                if (cross_origin && is_sensitive(m_extra_headers.headers[i].name)) continue;
                if (!req.set_header(m_extra_headers.headers[i].name, m_extra_headers.headers[i].value)) {
                    err = knst_network_error::HttpInvalidHeader; return false;
                }
            }

            if (!o.basic_user.empty() && !cross_origin && !req.has_header(u"Authorization")) {
                knst_byte_string userpass = knst_net_detail::to_utf8(o.basic_user);
                userpass.push_back(':');
                knst_byte_string pw = knst_net_detail::to_utf8(o.basic_pass);
                knst_net_detail::append_bytes(userpass, pw.data(), pw.length());
                knst_c16string auth = u"Basic ";
                auth.append(knst_net_detail::base64_encode_bytes(userpass));
                req.set_header_internal(u"Authorization", auth);
            }

            if (o.auto_cookie && !req.has_header(u"Cookie")) {
                std::lock_guard<std::mutex> lk(m_cookie_mutex);
                if (!m_cookies.empty()) {
                    int64_t now = now_unix_seconds();
                    knst_c16string cookie_hdr;
                    for (uint32_t i = 0; i < m_cookies.size(); ++i) {
                        const knst_cookie& c = m_cookies[i];
                        if (c.expires_unix > 0 && c.expires_unix < now) continue;
                        bool host_ok = c.host_only ? (c.domain == u.host) : domain_match(u.host, c.domain);
                        if (!host_ok) continue;
                        if (c.secure && u.scheme != u"https") continue;
                        if (!path_match(u.path, c.path)) continue;
                        if (cookie_hdr.length()) cookie_hdr.append(u"; ");
                        cookie_hdr.append(c.name);
                        cookie_hdr.append(u'=');
                        cookie_hdr.append(c.value);
                    }
                    if (!cookie_hdr.empty()) req.set_header_internal(u"Cookie", cookie_hdr);
                }
            }
            const bool method_has_body = (cur_method == u"POST" || cur_method == u"PUT" || cur_method == u"PATCH");
            if (o.use_chunked_upload && (method_has_body || body_n > 0)) {
                req.set_header_internal(u"Transfer-Encoding", u"chunked");
                if (body_n > 0 && !req.has_header(u"Content-Type"))
                    req.set_header_internal(u"Content-Type", o.default_content_type);
            } else if (body_n > 0) {
                knst_c16string clen; knst_net_detail::append_uint(clen, static_cast<uint32_t>(body_n));
                req.set_header_internal(u"Content-Length", clen);
                if (!req.has_header(u"Content-Type")) req.set_header_internal(u"Content-Type", o.default_content_type);
            } else if (method_has_body) {
                req.set_header_internal(u"Content-Length", u"0");
            }

            // ---- connect + exchange (with first-byte retry on stale pooled sockets) ----
            knst_http_response resp;
            const knst_c16string key = make_pool_key(u, pc, !o.allow_private_networks,o.bind_local_ip);
                                                     
            for (int attempt = 0; ; ++attempt) {
                bool reused = false;
                knst_tcp_socket* sock = open_connection(u, pc, o, req_timeout, attempt == 0, key, reused, err);
                if (!sock) return false;
                exchange_state st;
                bool ok = do_exchange(*sock, req, body_ptr, body_n, o, req_timeout, total_dl, resp, st, err);
                if (ok) {
                    if (!(st.reusable && m_pool.put(key, sock, o.max_per_host))) delete sock;
                    break;
                }
                delete sock;
                bool stale_err = err == knst_network_error::ConnectionClosed ||
                                 err == knst_network_error::HttpConnectionClosed ||
                                 err == knst_network_error::ConnectionReset ||
                                 err == knst_network_error::ConnectionAborted ||
                                 err == knst_network_error::BrokenPipe;
                if (reused && attempt == 0 && !st.got_byte && stale_err && is_idempotent(cur_method)) {
                    resp = knst_http_response{};
                    err = knst_network_error::None;
                    continue;
                }
                return false;
            }

            if (o.auto_cookie) collect_cookies(resp, u);

            if (resp.is_redirect() && o.follow_redirects) {
                knst_c16string loc = resp.get_header(u"Location");
                if (!loc.empty()) {
                    if (++hops > o.max_redirects) { err = knst_network_error::HttpTooManyRedirects; return false; }
                    if (resp.status == 303) {
                        if (cur_method != u"HEAD") cur_method = u"GET";
                        body_n = 0;
                    } else if ((resp.status == 301 || resp.status == 302) && cur_method == u"POST") {
                        cur_method = u"GET";
                        body_n = 0;
                    }
                    knst_url next_url;
                    if (!knst_url::resolve(u, loc, next_url, err)) {
                        if (err == knst_network_error::None) err = knst_network_error::HttpParseUrl;
                        return false;
                    }
                    cur_url = next_url.to_string(false);
                    continue;
                }
            }

            out = std::move(resp);
            return true;
        }
    }
};



/* ============================================================================
 * 18) knst_doh_resolver — DNS over HTTPS (RFC 8484)
 * ============================================================================ */

// ============================================================================
//  Public IP discovery — queries an external HTTPS service
// ----------------------------------------------------------------------------
//  Requires KNST_NETWORK_ENABLE_TLS (uses https://api.ipify.org).
//  Without TLS, both functions return TlsNotSupported and empty string.
// ============================================================================
namespace knst_net {

#if defined(KNST_NETWORK_ENABLE_TLS)
    inline knst_c16string public_ipv4(knst_network_error& err,
                                       uint32_t timeout_ms = 5000) noexcept {
        err = knst_network_error::None;
        knst_http_client c;
        knst_http_client::options o;
        o.timeout_ms       = timeout_ms;
        o.total_timeout_ms = timeout_ms * 2;
        o.follow_redirects = true;
        o.allow_content_encoding = true;

        knst_http_response r;
        if (!c.get(u"https://api.ipify.org", o, r, err)) return knst_c16string();
        if (r.status != 200 || r.body.empty()) {
            err = knst_network_error::ProtocolError;
            return knst_c16string();
        }
        return knst_c16string(reinterpret_cast<const char*>(r.body.data()),
                               r.body.length());
    }

    inline knst_c16string public_ipv6(knst_network_error& err,
                                       uint32_t timeout_ms = 5000) noexcept {
        err = knst_network_error::None;
        knst_http_client c;
        knst_http_client::options o;
        o.timeout_ms       = timeout_ms;
        o.total_timeout_ms = timeout_ms * 2;
        o.follow_redirects = true;
        o.allow_content_encoding = true;

        knst_http_response r;
        if (!c.get(u"https://api64.ipify.org", o, r, err)) return knst_c16string();
        if (r.status != 200 || r.body.empty()) {
            err = knst_network_error::ProtocolError;
            return knst_c16string();
        }
        knst_c16string ip(reinterpret_cast<const char*>(r.body.data()),
                           r.body.length());
        if (!ip.contains(u":")) return knst_c16string();
        return ip;
    }
#else
    inline knst_c16string public_ipv4(knst_network_error& err,
                                       uint32_t = 5000) noexcept {
        err = knst_network_error::TlsNotSupported;
        return knst_c16string();
    }
    inline knst_c16string public_ipv6(knst_network_error& err,
                                       uint32_t = 5000) noexcept {
        err = knst_network_error::TlsNotSupported;
        return knst_c16string();
    }
#endif

} // namespace knst_net

class knst_doh_resolver {
public:
    static bool resolve(const knst_c16string& doh_endpoint,
                        const knst_c16string& host,
                        uint16_t port,
                        knst_vector<knst_endpoint>& out,
                        knst_network_error& err,
                        uint32_t timeout_ms = 5000,
                        int family = AF_UNSPEC) noexcept {
        err = knst_network_error::None;
        out.clear();

        if (doh_endpoint.empty() || host.empty() || port == 0) {
            err = knst_network_error::InvalidArgument; return false;
        }

        {
            knst_network_error e2 = knst_network_error::None;
            knst_ip_address ip = knst_ip_address::from_string(host, e2);
            if (e2 == knst_network_error::None && ip.is_valid() &&
                (family == AF_UNSPEC || family == ip.family())) {
                if (!out.push_back(knst_endpoint(ip, port))) {
                    err = knst_network_error::OutOfMemory; return false;
                }
                return true;
            }
        }

#ifndef KNST_NETWORK_ENABLE_TLS
        err = knst_network_error::TlsNotSupported;
        return false;
#else
                bool need_a    = (family == AF_UNSPEC || family == AF_INET);
        bool need_aaaa = (family == AF_UNSPEC || family == AF_INET6);

        knst_network_error a_err = knst_network_error::None;
        if (need_a && !query_once(doh_endpoint, host, port, 1, out, a_err, timeout_ms)) {
            if (family == AF_INET) { err = a_err; return false; }
            // AF_UNSPEC: fall through, an IPv6-only host is legitimate
        }
        if (need_aaaa) {
            knst_network_error e2 = knst_network_error::None;
            if (!query_once(doh_endpoint, host, port, 28, out, e2, timeout_ms)) {
                if (out.size() == 0) {
                    err = (family == AF_INET6 || a_err == knst_network_error::None) ? e2 : a_err;
                    return false;
                }
            }
        }
        if (out.size() == 0) { err = knst_network_error::DnsNoData; return false; }
        return true;
#endif
    }

private:
    static bool query_once(const knst_c16string& doh_endpoint,
                           const knst_c16string& host,
                           uint16_t port,
                           uint16_t qtype,
                           knst_vector<knst_endpoint>& out,
                           knst_network_error& err,
                           uint32_t timeout_ms) noexcept {
        err = knst_network_error::None;

        uint64_t now_ns = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        uint16_t id = static_cast<uint16_t>((now_ns ^ (now_ns >> 16) ^ 0xA5A5u) & 0xFFFFu);

        knst_byte_string query;
        if (!knst_net_detail::dns_build_query(host, qtype, id, query)) {
            err = knst_network_error::InvalidArgument; return false;
        }

        knst_http_client client;
        knst_http_client::options opts;
        opts.timeout_ms             = timeout_ms;
        opts.total_timeout_ms       = timeout_ms;
        opts.allow_content_encoding = false;
        opts.user_agent             = u"KernelNucleusT/1.0 (DoH)";
        opts.auto_cookie            = false;
        opts.use_keepalive          = false;

        knst_http_header ct_hdr;
        ct_hdr.name  = u"Content-Type";
        ct_hdr.value = u"application/dns-message";
        opts.headers.push_back(ct_hdr);

        knst_http_header accept_hdr;
        accept_hdr.name  = u"Accept";
        accept_hdr.value = u"application/dns-message";
        opts.headers.push_back(accept_hdr);

        knst_http_response resp;
        if (!client.post(doh_endpoint, query, opts, resp, err)) return false;
        if (resp.status != 200) { err = knst_network_error::DnsFailure; return false; }

        knst_vector<knst_ip_address> ips;
        if (!knst_net_detail::dns_parse_response(resp.body, id, ips, err)) return false;

        for (uint32_t i = 0; i < ips.size(); ++i) {
            if (!out.push_back(knst_endpoint(ips[i], port))) {
                err = knst_network_error::OutOfMemory; return false;
            }
        }
        return true;
    }
};
