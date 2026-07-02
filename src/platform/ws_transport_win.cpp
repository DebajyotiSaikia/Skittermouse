// Windows client/server WebSocket transport over TCP + TLS (spec 5.1/5.5: wss://).
// Native WinSock2 + Schannel (SSPI), zero third-party. Built on the unit-tested WS
// handshake (net/ws_handshake) and frame codec (net/ws_frame, net/frame_assembler).
// TLS is encryption-only (self-signed cert, no verification) -- the app-layer secure
// link (PSK) is the trust gate, matching the validated POSIX/OpenSSL counterpart.
// Extensive sm::log lines make the (hardware-only) TLS path debuggable on real PCs.

#include "net/ws_transport.h"

#include "core/log.h"
#include "net/frame_assembler.h"
#include "net/ws_frame.h"
#include "net/ws_handshake.h"

#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// windows.h after winsock2.h to avoid the winsock1 clash.
#include <windows.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>
#include <wincrypt.h>

#include <cstring>
#include <string>
#include <vector>

namespace sm::net {

namespace {

inline void tlog(const std::string& m) { sm::log::write("[tls] " + m); }

// Start WinSock once for the process (OS ref-counts; cleaned up at process exit).
struct WsaInit {
    WsaInit() {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
    }
} g_wsaInit;

// Blocking write of all bytes to a raw (pre-TLS) socket -- used for handshake tokens.
bool socketSendAll(SOCKET s, const void* data, DWORD len) {
    const char* p = static_cast<const char*>(data);
    DWORD sent = 0;
    while (sent < len) {
        int n = ::send(s, p + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<DWORD>(n);
    }
    return true;
}

// Ephemeral self-signed cert (+ key) for the TLS server side. Encryption-only: the
// client never validates it. Returns nullptr on failure.
PCCERT_CONTEXT makeSelfSignedCert() {
    BYTE enc[512];
    DWORD encSize = sizeof(enc);
    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=Skittermouse", CERT_X500_NAME_STR, nullptr, enc,
                        &encSize, nullptr)) {
        tlog("CertStrToName failed");
        return nullptr;
    }
    CERT_NAME_BLOB subject{encSize, enc};
    // pKeyProvInfo = nullptr -> a fresh key is generated in a temp container and the
    // cert's key-prov-info property is set so Schannel can find the private key.
    PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(0, &subject, 0, nullptr, nullptr, nullptr,
                                                        nullptr, nullptr);
    if (!cert) tlog("CertCreateSelfSignCertificate failed err=" + std::to_string(GetLastError()));
    return cert;
}

// Schannel TLS channel over one connected socket. Mirrors the OpenSSL logic in
// ws_transport_posix.cpp (client SSL_connect / server SSL_accept, then stream I/O).
class SchannelTls {
public:
    ~SchannelTls() { dispose(); }

    bool clientHandshake(SOCKET s, const std::string& host) {
        SCHANNEL_CRED cred{};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
        if (AcquireCredentialsHandleW(nullptr, const_cast<SEC_WCHAR*>(UNISP_NAME_W),
                                      SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr, nullptr,
                                      &cred_, nullptr) != SEC_E_OK) {
            tlog("client AcquireCredentialsHandle failed");
            return false;
        }
        haveCred_ = true;
        return handshake(s, /*server*/ false, host);
    }

    bool serverHandshake(SOCKET s) {
        serverCert_ = makeSelfSignedCert();
        if (!serverCert_) return false;
        SCHANNEL_CRED cred{};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.cCreds = 1;
        cred.paCred = &serverCert_;
        if (AcquireCredentialsHandleW(nullptr, const_cast<SEC_WCHAR*>(UNISP_NAME_W),
                                      SECPKG_CRED_INBOUND, nullptr, &cred, nullptr, nullptr, &cred_,
                                      nullptr) != SEC_E_OK) {
            tlog("server AcquireCredentialsHandle failed");
            return false;
        }
        haveCred_ = true;
        return handshake(s, /*server*/ true, "");
    }

    // Encrypt + send `len` bytes (blocking). Returns false on error.
    bool sendData(SOCKET s, const uint8_t* data, std::size_t len) {
        while (len > 0) {
            DWORD chunk = static_cast<DWORD>(len < sizes_.cbMaximumMessage ? len
                                                                           : sizes_.cbMaximumMessage);
            std::vector<uint8_t> buf(sizes_.cbHeader + chunk + sizes_.cbTrailer);
            std::memcpy(buf.data() + sizes_.cbHeader, data, chunk);
            SecBuffer sb[4];
            sb[0] = {sizes_.cbHeader, SECBUFFER_STREAM_HEADER, buf.data()};
            sb[1] = {chunk, SECBUFFER_DATA, buf.data() + sizes_.cbHeader};
            sb[2] = {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER,
                     buf.data() + sizes_.cbHeader + chunk};
            sb[3] = {0, SECBUFFER_EMPTY, nullptr};
            SecBufferDesc desc{SECBUFFER_VERSION, 4, sb};
            SECURITY_STATUS ss = EncryptMessage(&ctx_, 0, &desc, 0);
            if (ss != SEC_E_OK) {
                tlog("EncryptMessage failed 0x" + std::to_string(ss));
                return false;
            }
            DWORD total = sb[0].cbBuffer + sb[1].cbBuffer + sb[2].cbBuffer;
            if (!socketSendAll(s, buf.data(), total)) return false;
            data += chunk;
            len -= chunk;
        }
        return true;
    }

    // Decrypt available data. Returns >0 bytes into out, 0 on clean close, or -1 with
    // WSASetLastError(WSAEWOULDBLOCK) when no full record is ready yet.
    long recvData(SOCKET s, void* out, std::size_t cap) {
        if (!plain_.empty()) return serve(out, cap);
        for (;;) {
            if (!enc_.empty()) {
                SecBuffer sb[4];
                sb[0] = {static_cast<DWORD>(enc_.size()), SECBUFFER_DATA, enc_.data()};
                sb[1] = {0, SECBUFFER_EMPTY, nullptr};
                sb[2] = {0, SECBUFFER_EMPTY, nullptr};
                sb[3] = {0, SECBUFFER_EMPTY, nullptr};
                SecBufferDesc desc{SECBUFFER_VERSION, 4, sb};
                SECURITY_STATUS ss = DecryptMessage(&ctx_, &desc, 0, nullptr);
                if (ss == SEC_E_OK) {
                    std::vector<uint8_t> extra;
                    for (int i = 1; i < 4; ++i) {
                        if (sb[i].BufferType == SECBUFFER_DATA && sb[i].cbBuffer)
                            plain_.insert(plain_.end(), static_cast<uint8_t*>(sb[i].pvBuffer),
                                          static_cast<uint8_t*>(sb[i].pvBuffer) + sb[i].cbBuffer);
                        else if (sb[i].BufferType == SECBUFFER_EXTRA && sb[i].cbBuffer)
                            extra.assign(static_cast<uint8_t*>(sb[i].pvBuffer),
                                         static_cast<uint8_t*>(sb[i].pvBuffer) + sb[i].cbBuffer);
                    }
                    enc_.swap(extra);
                    if (!plain_.empty()) return serve(out, cap);
                    continue;
                }
                if (ss == SEC_I_CONTEXT_EXPIRED) return 0; // peer closed the TLS session
                if (ss != SEC_E_INCOMPLETE_MESSAGE) {
                    tlog("DecryptMessage failed 0x" + std::to_string(ss));
                    return 0; // fatal -> treat as closed
                }
                // fall through: need more encrypted bytes
            }
            char tmp[8192];
            int n = ::recv(s, tmp, sizeof(tmp), 0);
            if (n == 0) return 0;
            if (n < 0) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    WSASetLastError(WSAEWOULDBLOCK);
                    return -1;
                }
                return 0;
            }
            enc_.insert(enc_.end(), tmp, tmp + n);
        }
    }

    void shutdownTls(SOCKET s) {
        if (!haveCtx_) return;
        DWORD type = SCHANNEL_SHUTDOWN;
        SecBuffer sb{sizeof(type), SECBUFFER_TOKEN, &type};
        SecBufferDesc desc{SECBUFFER_VERSION, 1, &sb};
        if (ApplyControlToken(&ctx_, &desc) != SEC_E_OK) return;
        SecBuffer out{0, SECBUFFER_TOKEN, nullptr};
        SecBufferDesc outDesc{SECBUFFER_VERSION, 1, &out};
        DWORD flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
        DWORD of = 0;
        if (InitializeSecurityContextW(&cred_, &ctx_, nullptr, flags, 0, 0, nullptr, 0, nullptr,
                                       &outDesc, &of, nullptr) == SEC_E_OK &&
            out.pvBuffer) {
            socketSendAll(s, out.pvBuffer, out.cbBuffer);
            FreeContextBuffer(out.pvBuffer);
        }
    }

private:
    long serve(void* out, std::size_t cap) {
        std::size_t n = plain_.size() < cap ? plain_.size() : cap;
        std::memcpy(out, plain_.data(), n);
        plain_.erase(plain_.begin(), plain_.begin() + n);
        return static_cast<long>(n);
    }

    // The SSPI handshake token loop, shared by client (InitializeSecurityContext) and
    // server (AcceptSecurityContext) roles. Runs on the blocking socket.
    bool handshake(SOCKET s, bool server, const std::string& host) {
        const DWORD flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY |
                            ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM |
                            ISC_REQ_MANUAL_CRED_VALIDATION;
        std::wstring whost(host.begin(), host.end());
        std::vector<uint8_t> in;
        bool first = true;
        for (;;) {
            SecBuffer inBuf[2];
            inBuf[0] = {static_cast<DWORD>(in.size()), SECBUFFER_TOKEN,
                        in.empty() ? nullptr : in.data()};
            inBuf[1] = {0, SECBUFFER_EMPTY, nullptr};
            SecBufferDesc inDesc{SECBUFFER_VERSION, 2, inBuf};
            SecBuffer outBuf{0, SECBUFFER_TOKEN, nullptr};
            SecBufferDesc outDesc{SECBUFFER_VERSION, 1, &outBuf};
            DWORD of = 0;
            SECURITY_STATUS ss;
            if (server) {
                ss = AcceptSecurityContext(&cred_, haveCtx_ ? &ctx_ : nullptr,
                                           first ? nullptr : &inDesc, flags, 0,
                                           haveCtx_ ? nullptr : &ctx_, &outDesc, &of, nullptr);
            } else {
                ss = InitializeSecurityContextW(
                    &cred_, haveCtx_ ? &ctx_ : nullptr,
                    whost.empty() ? nullptr : const_cast<SEC_WCHAR*>(whost.c_str()), flags, 0, 0,
                    first ? nullptr : &inDesc, 0, haveCtx_ ? nullptr : &ctx_, &outDesc, &of, nullptr);
            }
            haveCtx_ = true;
            first = false;

            if (outBuf.cbBuffer && outBuf.pvBuffer) {
                bool sok = socketSendAll(s, outBuf.pvBuffer, outBuf.cbBuffer);
                FreeContextBuffer(outBuf.pvBuffer);
                if (!sok) return false;
            }

            if (ss == SEC_E_OK) {
                // Preserve any application data that trailed the final token.
                if (inBuf[1].BufferType == SECBUFFER_EXTRA && inBuf[1].cbBuffer)
                    enc_.assign(in.end() - inBuf[1].cbBuffer, in.end());
                if (QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_) != SEC_E_OK)
                    return false;
                tlog(std::string(server ? "server" : "client") + " TLS handshake OK");
                return true;
            }
            if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) {
                if (ss == SEC_I_CONTINUE_NEEDED) {
                    if (inBuf[1].BufferType == SECBUFFER_EXTRA && inBuf[1].cbBuffer) {
                        std::vector<uint8_t> extra(in.end() - inBuf[1].cbBuffer, in.end());
                        in.swap(extra);
                    } else {
                        in.clear();
                    }
                }
                char tmp[8192];
                int n = ::recv(s, tmp, sizeof(tmp), 0);
                if (n <= 0) {
                    tlog("handshake recv failed n=" + std::to_string(n));
                    return false;
                }
                in.insert(in.end(), tmp, tmp + n);
                continue;
            }
            tlog(std::string(server ? "Accept" : "Initialize") +
                 "SecurityContext failed 0x" + std::to_string(ss));
            return false;
        }
    }

    void dispose() {
        if (haveCtx_) { DeleteSecurityContext(&ctx_); haveCtx_ = false; }
        if (haveCred_) { FreeCredentialsHandle(&cred_); haveCred_ = false; }
        if (serverCert_) { CertFreeCertificateContext(serverCert_); serverCert_ = nullptr; }
    }

    CredHandle cred_{};
    CtxtHandle ctx_{};
    SecPkgContext_StreamSizes sizes_{};
    bool haveCred_ = false;
    bool haveCtx_ = false;
    PCCERT_CONTEXT serverCert_ = nullptr;
    std::vector<uint8_t> enc_;   // received, not-yet-decrypted
    std::vector<uint8_t> plain_; // decrypted, not-yet-consumed
};

// One WebSocket transport over a connected TCP socket. Per RFC 6455, client-role
// frames are masked and server-role (accepted) frames are not.
class WinWsTransport : public Transport {
public:
    WinWsTransport(SOCKET sock = INVALID_SOCKET, bool client = true)
        : sock_(sock), client_(client), connected_(sock != INVALID_SOCKET) {}

    ~WinWsTransport() override { close(); }

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
        if (sock_ == INVALID_SOCKET) { freeaddrinfo(res); return false; }
        // Non-blocking connect with a bounded timeout, so dialing an offline peer
        // can't stall the dial loop (or app shutdown) for the full OS SYN timeout.
        u_long nb = 1;
        ioctlsocket(sock_, FIONBIO, &nb);
        int cr = ::connect(sock_, res->ai_addr, static_cast<int>(res->ai_addrlen));
        bool connectedOk = (cr == 0);
        if (!connectedOk && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wr;
            FD_ZERO(&wr);
            FD_SET(sock_, &wr);
            timeval tv{3, 0}; // 3s connect timeout
            if (select(0, nullptr, &wr, nullptr, &tv) > 0 && FD_ISSET(sock_, &wr)) {
                int err = 0, len = sizeof(err);
                getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
                connectedOk = (err == 0);
            }
        }
        u_long bl = 0;
        ioctlsocket(sock_, FIONBIO, &bl); // blocking again for the handshake reads
        if (!connectedOk) {
            freeaddrinfo(res);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
        freeaddrinfo(res);

        // TLS first (spec 5.1: wss://). Socket is blocking here so the SSPI token
        // exchange runs to completion; the WS HTTP handshake then rides over TLS.
        if (!tls_.clientHandshake(sock_, host)) {
            tlog("client TLS handshake failed to " + host);
            close();
            return false;
        }
        tlsActive_ = true;

        std::string key = wsGenerateClientKey();
        std::string req = wsBuildClientHandshake(host + ":" + portStr, "/input", key);
        if (!sendAll(reinterpret_cast<const uint8_t*>(req.data()), req.size())) return false;
        std::string resp;
        char c;
        while (resp.find("\r\n\r\n") == std::string::npos) {
            long n = recvRaw(&c, 1);
            if (n <= 0) return false;
            resp += c;
            if (resp.size() > 8192) return false;
        }
        if (resp.find(wsAcceptKey(key)) == std::string::npos) return false;
        // The handshake above used blocking reads; switch to non-blocking now so
        // recv() (whose WSAEWOULDBLOCK path returns 0) can be polled by the mesh
        // loop without ever blocking it.
        u_long nonBlocking = 1;
        ioctlsocket(sock_, FIONBIO, &nonBlocking);
        connected_ = true;
        return true;
    }

    bool isConnected() const override { return connected_; }

    bool send(const uint8_t* data, std::size_t len) override {
        if (!connected_) return false;
        Bytes frame = wsEncodeFrame(WsOpcode::Binary, data, len, /*masked*/ client_);
        return sendAll(frame.data(), frame.size());
    }

    int recv(uint8_t* buf, std::size_t cap) override {
        if (!connected_) return -1;
        WsFrame out;
        if (assembler_.next(out)) return copyFrame(out, buf, cap);
        char tmp[4096];
        long n = recvRaw(tmp, sizeof(tmp));
        if (n == 0) { connected_ = false; return -1; }
        if (n < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
            connected_ = false;
            return -1;
        }
        assembler_.feed(reinterpret_cast<const uint8_t*>(tmp), static_cast<std::size_t>(n));
        if (assembler_.next(out)) return copyFrame(out, buf, cap);
        return 0;
    }

    void close() override {
        if (sock_ != INVALID_SOCKET) {
            if (tlsActive_) tls_.shutdownTls(sock_);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        connected_ = false;
    }

    // Server-side post-accept sequence: TLS handshake, then the WS HTTP handshake
    // over TLS. Called by wsAcceptOne on the accepted (blocking) socket.
    bool acceptServerSide() {
        if (!tls_.serverHandshake(sock_)) {
            tlog("server TLS handshake failed");
            return false;
        }
        tlsActive_ = true;
        std::string req;
        char c;
        while (req.find("\r\n\r\n") == std::string::npos) {
            long n = recvRaw(&c, 1);
            if (n <= 0) return false;
            req += c;
            if (req.size() > 8192) return false;
        }
        std::string key = parseKeyFromRequest(req);
        if (key.empty()) return false;
        std::string resp = wsBuildServerResponse(key);
        if (!sendAll(reinterpret_cast<const uint8_t*>(resp.data()), resp.size())) return false;
        u_long nonBlocking = 1;
        ioctlsocket(sock_, FIONBIO, &nonBlocking);
        connected_ = true;
        return true;
    }

private:
    int copyFrame(const WsFrame& f, uint8_t* buf, std::size_t cap) {
        std::size_t n = f.payload.size() <= cap ? f.payload.size() : cap;
        std::memcpy(buf, f.payload.data(), n);
        return static_cast<int>(n);
    }
    // Read raw application bytes: through TLS (decrypt) when active, else straight
    // off the socket. Returns >0 bytes, 0 on clean close, -1 (WSAEWOULDBLOCK) if none
    // ready. Identical contract for both paths so recv()'s logic is unchanged.
    long recvRaw(void* buf, std::size_t cap) {
        if (tlsActive_) return tls_.recvData(sock_, buf, cap);
        int n = ::recv(sock_, static_cast<char*>(buf), static_cast<int>(cap), 0);
        return n;
    }
    bool sendAll(const uint8_t* d, std::size_t len) {
        if (tlsActive_) return tls_.sendData(sock_, d, len);
        std::size_t sent = 0;
        while (sent < len) {
            int n = ::send(sock_, reinterpret_cast<const char*>(d) + sent,
                           static_cast<int>(len - sent), 0);
            if (n == SOCKET_ERROR) {
                // Non-blocking socket with a full send buffer: wait briefly for
                // writability, then retry. Bounded so a wedged peer can't hang us.
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    fd_set wr;
                    FD_ZERO(&wr);
                    FD_SET(sock_, &wr);
                    timeval tv{0, 100000}; // 100 ms
                    if (select(0, nullptr, &wr, nullptr, &tv) <= 0) return false;
                    continue;
                }
                return false;
            }
            if (n == 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }
    // Extract the Sec-WebSocket-Key value from a client handshake request.
    static std::string parseKeyFromRequest(const std::string& req) {
        const std::string tag = "Sec-WebSocket-Key:";
        std::size_t p = req.find(tag);
        if (p == std::string::npos) return {};
        p += tag.size();
        while (p < req.size() && (req[p] == ' ' || req[p] == '\t')) ++p;
        std::size_t e = req.find("\r\n", p);
        if (e == std::string::npos) return {};
        return req.substr(p, e - p);
    }

    SOCKET sock_ = INVALID_SOCKET;
    bool client_ = true;
    bool connected_ = false;
    bool tlsActive_ = false;
    SchannelTls tls_;
    WsFrameAssembler assembler_;
};

} // namespace

Transport* createWsClientTransport() { return new WinWsTransport(INVALID_SOCKET, true); }

Transport* wsAcceptOne(uint16_t port, int timeoutMs, std::string* outPeerIp) {
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) return nullptr;
    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes),
               sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listenSock, 1) != 0) {
        closesocket(listenSock);
        return nullptr;
    }

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(listenSock, &rd);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (select(0, &rd, nullptr, nullptr, &tv) <= 0) {
        closesocket(listenSock);
        return nullptr;
    }

    SOCKET client = accept(listenSock, nullptr, nullptr);
    closesocket(listenSock);
    if (client == INVALID_SOCKET) return nullptr;

    if (outPeerIp) {
        sockaddr_in pa{};
        int palen = sizeof(pa);
        char ip[INET_ADDRSTRLEN] = "";
        if (getpeername(client, reinterpret_cast<sockaddr*>(&pa), &palen) == 0 &&
            inet_ntop(AF_INET, &pa.sin_addr, ip, sizeof(ip)))
            *outPeerIp = ip;
    }

    // The accepted socket is blocking; acceptServerSide runs the TLS + WS handshakes
    // to completion, then flips it non-blocking for the mesh poll loop.
    auto* t = new WinWsTransport(client, /*client role*/ false);
    if (!t->acceptServerSide()) {
        delete t; // dtor closes the socket
        return nullptr;
    }
    return t;
}

} // namespace sm::net
