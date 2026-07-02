#include "net/discovery_socket.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace sm::net {

namespace {

#ifdef _WIN32
struct WsaScope {
    bool ok;
    WsaScope() {
        WSADATA w;
        ok = (WSAStartup(MAKEWORD(2, 2), &w) == 0);
    }
    ~WsaScope() {
        if (ok) WSACleanup();
    }
};
void closeSock(SOCKET s) { closesocket(s); }
#else
using SOCKET = int;
constexpr int INVALID_SOCKET = -1;
struct WsaScope {
    bool ok = true;
};
void closeSock(int s) { ::close(s); }
#endif

} // namespace

bool broadcastBeacon(const Beacon& b, uint16_t port) {
    WsaScope wsa;
    if (!wsa.ok) return false;

    Bytes pkt = encodeBeacon(b);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    int sent = sendto(s, reinterpret_cast<const char*>(pkt.data()),
                      static_cast<int>(pkt.size()), 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    closeSock(s);
    return sent == static_cast<int>(pkt.size());
}

bool receiveBeacon(uint16_t port, int timeout_ms, Beacon& out) {
    WsaScope wsa;
    if (!wsa.ok) return false;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSock(s);
        return false;
    }

#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    uint8_t buf[2048];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLen);
    closeSock(s);
    if (n <= 0) return false;
    return decodeBeacon(buf, static_cast<std::size_t>(n), out);
}

} // namespace sm::net
