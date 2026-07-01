#include "net/wol_sender.h"

#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sm::net {

Bytes buildMagicPacket(const Mac& mac) {
    Bytes p;
    p.reserve(102);
    for (int i = 0; i < 6; ++i) p.push_back(0xFF);
    for (int rep = 0; rep < 16; ++rep)
        p.insert(p.end(), mac.begin(), mac.end());
    return p;
}

bool parseMac(const std::string& s, Mac& out) {
    int values[6];
    char sep1 = 0, sep2 = 0, sep3 = 0, sep4 = 0, sep5 = 0, extra = 0;
    // Accept ':' or '-' as the (single, consistent) separator. The trailing %c
    // captures any extra character so "..:06:07" (too long) is rejected: a fully
    // consumed 6-group MAC matches exactly 11 fields, trailing junk makes it 12.
    int n = std::sscanf(s.c_str(), "%x%c%x%c%x%c%x%c%x%c%x%c",
                        &values[0], &sep1, &values[1], &sep2, &values[2], &sep3,
                        &values[3], &sep4, &values[4], &sep5, &values[5], &extra);
    if (n != 11) return false;
    char sep = sep1;
    if ((sep != ':' && sep != '-') || sep2 != sep || sep3 != sep || sep4 != sep || sep5 != sep)
        return false;
    for (int i = 0; i < 6; ++i) {
        if (values[i] < 0 || values[i] > 255) return false;
        out[i] = static_cast<uint8_t>(values[i]);
    }
    return true;
}

bool sendMagicPacket(const Mac& mac, const std::string& broadcastIp, uint16_t port) {
    Bytes packet = buildMagicPacket(mac);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { WSACleanup(); return false; }
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, broadcastIp.c_str(), &addr.sin_addr);
    int sent = sendto(s, reinterpret_cast<const char*>(packet.data()),
                      static_cast<int>(packet.size()), 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    closesocket(s);
    WSACleanup();
    return sent == static_cast<int>(packet.size());
#else
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return false;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, broadcastIp.c_str(), &addr.sin_addr);
    ssize_t sent = sendto(s, packet.data(), packet.size(), 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(s);
    return sent == static_cast<ssize_t>(packet.size());
#endif
}

} // namespace sm::net
