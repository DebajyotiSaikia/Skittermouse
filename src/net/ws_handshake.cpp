#include "net/ws_handshake.h"

#include "crypto/crypto.h"

namespace sm::net {

namespace {
const char* kMagicGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; // RFC 6455
}

std::string wsAcceptKey(const std::string& clientKey) {
    std::string s = clientKey + kMagicGuid;
    auto h = sm::crypto::sha1(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return sm::crypto::base64Encode(h.data(), h.size());
}

std::string wsGenerateClientKey() {
    auto r = sm::crypto::randomBytes(16);
    return sm::crypto::base64Encode(r.data(), r.size());
}

std::string wsBuildClientHandshake(const std::string& host, const std::string& path,
                                   const std::string& key) {
    return "GET " + path + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: " + key + "\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n";
}

std::string wsBuildServerResponse(const std::string& clientKey) {
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " + wsAcceptKey(clientKey) + "\r\n\r\n";
}

} // namespace sm::net
