// POSIX client/server WebSocket transport over TCP (spec 5.1/5.5). BSD sockets, so
// it serves both the macOS product build and the Linux container test rig. Mirrors
// platform/ws_transport_win.cpp exactly (same handshake, frame codec, non-blocking
// recv contract) so a node on either OS interoperates on the wire.

#include "net/ws_transport.h"

#include "net/frame_assembler.h"
#include "net/ws_frame.h"
#include "net/ws_handshake.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace sm::net {

namespace {

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// --- TLS (spec 5.1: wss://) via OpenSSL for the macOS product + Linux test rig.
// Peer identity is authenticated by the app-layer secure link (PSK), so TLS here is
// encryption-only: the server presents an ephemeral self-signed cert and the client
// does not verify it -- the PSK remains the trust gate. NOTE: the Windows backend now
// runs plain WS (no transport TLS), so this must be reconciled before Win<->Mac pairing.
SSL_CTX* clientTlsCtx() {
    static SSL_CTX* ctx = [] {
        SSL_CTX* c = SSL_CTX_new(TLS_client_method());
        if (c) SSL_CTX_set_verify(c, SSL_VERIFY_NONE, nullptr);
        return c;
    }();
    return ctx;
}

bool installSelfSignedCert(SSL_CTX* ctx) {
    EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    if (!pkey) return false;
    X509* x = X509_new();
    bool ok = false;
    if (x) {
        ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
        X509_gmtime_adj(X509_getm_notBefore(x), 0);
        X509_gmtime_adj(X509_getm_notAfter(x), 60L * 60 * 24 * 3650); // ~10 years
        X509_set_pubkey(x, pkey);
        X509_NAME* nm = X509_get_subject_name(x);
        X509_NAME_add_entry_by_txt(nm, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("Skittermouse"), -1, -1,
                                   0);
        X509_set_issuer_name(x, nm);
        ok = X509_sign(x, pkey, EVP_sha256()) && SSL_CTX_use_certificate(ctx, x) == 1 &&
             SSL_CTX_use_PrivateKey(ctx, pkey) == 1;
    }
    if (x) X509_free(x);
    EVP_PKEY_free(pkey);
    return ok;
}

SSL_CTX* serverTlsCtx() {
    static SSL_CTX* ctx = [] {
        SSL_CTX* c = SSL_CTX_new(TLS_server_method());
        if (c) installSelfSignedCert(c);
        return c;
    }();
    return ctx;
}

// One WebSocket transport over a connected TCP socket. Per RFC 6455, client-role
// frames are masked and server-role (accepted) frames are not.
class PosixWsTransport : public Transport {
public:
    PosixWsTransport(int sock = -1, bool client = true, SSL* ssl = nullptr)
        : sock_(sock), client_(client), connected_(sock >= 0), ssl_(ssl) {}

    ~PosixWsTransport() override { close(); }

    bool connect(const std::string& host, uint16_t port) override {
        if (connected_) return true;
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* res = nullptr;
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) return false;
        sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock_ < 0) { freeaddrinfo(res); return false; }
        // Non-blocking connect with a bounded timeout, so dialing an offline peer
        // can't stall the dial loop (or app shutdown) for the full OS SYN timeout.
        setNonBlocking(sock_);
        int cr = ::connect(sock_, res->ai_addr, res->ai_addrlen);
        bool connectedOk = (cr == 0);
        if (!connectedOk && (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
            fd_set wr;
            FD_ZERO(&wr);
            FD_SET(sock_, &wr);
            timeval tv{3, 0}; // 3s connect timeout
            if (select(sock_ + 1, nullptr, &wr, nullptr, &tv) > 0 && FD_ISSET(sock_, &wr)) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(sock_, SOL_SOCKET, SO_ERROR, &err, &len);
                connectedOk = (err == 0);
            }
        }
        // Back to blocking for the handshake reads (recv switches to non-blocking
        // again once the WebSocket upgrade completes).
        {
            int flags = fcntl(sock_, F_GETFL, 0);
            if (flags >= 0) fcntl(sock_, F_SETFL, flags & ~O_NONBLOCK);
        }
        if (!connectedOk) {
            freeaddrinfo(res);
            ::close(sock_);
            sock_ = -1;
            return false;
        }
        freeaddrinfo(res);

        // TLS handshake (wss://) on the connected, blocking socket before the WS upgrade.
        ssl_ = SSL_new(clientTlsCtx());
        if (!ssl_) { ::close(sock_); sock_ = -1; return false; }
        SSL_set_fd(ssl_, sock_);
        if (SSL_connect(ssl_) != 1) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            ::close(sock_);
            sock_ = -1;
            return false;
        }

        std::string key = wsGenerateClientKey();
        std::string req = wsBuildClientHandshake(host + ":" + portStr, "/input", key);
        if (!rawSendAll(reinterpret_cast<const uint8_t*>(req.data()), req.size())) return false;
        std::string resp;
        char c;
        while (resp.find("\r\n\r\n") == std::string::npos) {
            long n = rawRecv(&c, 1);
            if (n <= 0) return false;
            resp += c;
            if (resp.size() > 8192) return false;
        }
        if (resp.find(wsAcceptKey(key)) == std::string::npos) return false;
        setNonBlocking(sock_); // recv() below is non-blocking (returns 0 when idle)
        connected_ = true;
        return true;
    }

    bool isConnected() const override { return connected_; }

    bool send(const uint8_t* data, std::size_t len) override {
        if (!connected_) return false;
        Bytes frame = wsEncodeFrame(WsOpcode::Binary, data, len, /*masked*/ client_);
        return rawSendAll(frame.data(), frame.size());
    }

    int recv(uint8_t* buf, std::size_t cap) override {
        if (!connected_) return -1;
        WsFrame out;
        if (assembler_.next(out)) return copyFrame(out, buf, cap);
        char tmp[4096];
        long n = rawRecv(tmp, sizeof(tmp));
        if (n == 0) { connected_ = false; return -1; }
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
            connected_ = false;
            return -1;
        }
        assembler_.feed(reinterpret_cast<const uint8_t*>(tmp), static_cast<std::size_t>(n));
        if (assembler_.next(out)) return copyFrame(out, buf, cap);
        return 0;
    }

    void close() override {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (sock_ >= 0) {
            ::close(sock_);
            sock_ = -1;
        }
        connected_ = false;
    }

    // Server-side WS opening handshake, read/written over the (already-established)
    // TLS stream. Called by wsAcceptOne after SSL_accept. False on a bad request.
    bool serverHandshake() {
        std::string req;
        char c;
        while (req.find("\r\n\r\n") == std::string::npos) {
            long n = rawRecv(&c, 1);
            if (n <= 0) return false;
            req += c;
            if (req.size() > 8192) return false;
        }
        const std::string tag = "Sec-WebSocket-Key:";
        std::size_t p = req.find(tag);
        if (p == std::string::npos) return false;
        p += tag.size();
        while (p < req.size() && (req[p] == ' ' || req[p] == '\t')) ++p;
        std::size_t e = req.find("\r\n", p);
        if (e == std::string::npos) return false;
        std::string key = req.substr(p, e - p);
        if (key.empty()) return false;
        std::string resp = wsBuildServerResponse(key);
        if (!rawSendAll(reinterpret_cast<const uint8_t*>(resp.data()), resp.size())) return false;
        setNonBlocking(sock_);
        connected_ = true;
        return true;
    }

private:
    int copyFrame(const WsFrame& f, uint8_t* buf, std::size_t cap) {
        std::size_t n = f.payload.size() <= cap ? f.payload.size() : cap;
        std::memcpy(buf, f.payload.data(), n);
        return static_cast<int>(n);
    }
    long rawRecv(void* buf, std::size_t n) {
        if (!ssl_) return ::recv(sock_, buf, n, 0);
        int r = SSL_read(ssl_, buf, static_cast<int>(n));
        if (r > 0) return r;
        int e = SSL_get_error(ssl_, r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            errno = EWOULDBLOCK;
            return -1;
        }
        return 0; // clean close or fatal error -> EOF
    }

    bool rawSendAll(const uint8_t* d, std::size_t len) {
        std::size_t sent = 0;
        while (sent < len) {
            int r;
            if (ssl_) r = SSL_write(ssl_, d + sent, static_cast<int>(len - sent));
            else r = static_cast<int>(::send(sock_, d + sent, len - sent, 0));
            if (r > 0) {
                sent += static_cast<std::size_t>(r);
                continue;
            }
            bool wouldBlock;
            if (ssl_) {
                int e = SSL_get_error(ssl_, r);
                wouldBlock = (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ);
            } else {
                wouldBlock = (errno == EWOULDBLOCK || errno == EAGAIN);
            }
            if (!wouldBlock) return false;
            fd_set wr;
            FD_ZERO(&wr);
            FD_SET(sock_, &wr);
            timeval tv{0, 100000}; // 100 ms
            if (select(sock_ + 1, nullptr, &wr, nullptr, &tv) <= 0) return false;
        }
        return true;
    }

    int sock_ = -1;
    bool client_ = true;
    bool connected_ = false;
    SSL* ssl_ = nullptr;
    WsFrameAssembler assembler_;
};

} // namespace

Transport* createWsClientTransport() { return new PosixWsTransport(-1, true); }

Transport* wsAcceptOne(uint16_t port, int timeoutMs, std::string* outPeerIp) {
    int listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock < 0) return nullptr;
    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listenSock, 1) != 0) {
        ::close(listenSock);
        return nullptr;
    }

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(listenSock, &rd);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (select(listenSock + 1, &rd, nullptr, nullptr, &tv) <= 0) {
        ::close(listenSock);
        return nullptr;
    }

    int client = accept(listenSock, nullptr, nullptr);
    ::close(listenSock);
    if (client < 0) return nullptr;

    if (outPeerIp) {
        sockaddr_in pa{};
        socklen_t palen = sizeof(pa);
        char ip[INET_ADDRSTRLEN] = "";
        if (getpeername(client, reinterpret_cast<sockaddr*>(&pa), &palen) == 0 &&
            inet_ntop(AF_INET, &pa.sin_addr, ip, sizeof(ip)))
            *outPeerIp = ip;
    }

    // TLS handshake (wss://) on the accepted, blocking socket, then the WS upgrade
    // over the TLS stream (both inside the transport's serverHandshake()).
    SSL* ssl = SSL_new(serverTlsCtx());
    if (!ssl) { ::close(client); return nullptr; }
    SSL_set_fd(ssl, client);
    if (SSL_accept(ssl) != 1) {
        SSL_free(ssl);
        ::close(client);
        return nullptr;
    }
    PosixWsTransport* t = new PosixWsTransport(client, /*client role*/ false, ssl);
    if (!t->serverHandshake()) {
        delete t;
        return nullptr;
    }
    return t;
}

} // namespace sm::net
