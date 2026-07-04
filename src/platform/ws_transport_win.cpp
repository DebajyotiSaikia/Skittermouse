// Windows client/server WebSocket transport over plain TCP (spec 5.1/5.5).
// Native WinSock2, zero third-party. Built on the unit-tested WS handshake
// (net/ws_handshake) and frame codec (net/ws_frame, net/frame_assembler).
// Transport-level TLS is intentionally omitted: every message is already encrypted
// end-to-end by net/encrypted_transport (AES-256-GCM under the pairing PSK) and
// pairing is gated by the 6-digit numeric comparison, so wss would add no security.

#include "net/ws_transport.h"

#include "net/frame_assembler.h"
#include "net/ws_frame.h"
#include "net/ws_handshake.h"

#include <winsock2.h>
#include <ws2tcpip.h>
// windows.h after winsock2.h to avoid the winsock1 clash.
#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace sm::net {

namespace {

// Start WinSock once for the process (OS ref-counts; cleaned up at process exit).
struct WsaInit {
    WsaInit() {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
    }
} g_wsaInit;

// Disable Nagle so a single small input frame (a 12-byte mouse move) is sent
// immediately rather than coalesced -- Nagle can add tens of ms of pointer lag on
// the interactive input channel (spec 5.1).
void setLowLatency(SOCKET s) {
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay),
               sizeof(nodelay));
}

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
        setLowLatency(sock_); // TCP_NODELAY: never coalesce tiny input packets (spec 5.1)
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
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        connected_ = false;
    }

    // Server-side post-accept sequence: the WS HTTP handshake on the accepted
    // (blocking) socket. Called by wsAcceptOne.
    bool acceptServerSide() {
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
    // Read raw application bytes off the socket. Returns >0 bytes, 0 on clean close,
    // -1 (WSAEWOULDBLOCK) when none ready.
    long recvRaw(void* buf, std::size_t cap) {
        return ::recv(sock_, static_cast<char*>(buf), static_cast<int>(cap), 0);
    }
    bool sendAll(const uint8_t* d, std::size_t len) {
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

    // The accepted socket is blocking; acceptServerSide runs the WS handshake to
    // completion, then flips it non-blocking for the mesh poll loop.
    auto* t = new WinWsTransport(client, /*client role*/ false);
    if (!t->acceptServerSide()) {
        delete t; // dtor closes the socket
        return nullptr;
    }
    return t;
}

} // namespace sm::net
