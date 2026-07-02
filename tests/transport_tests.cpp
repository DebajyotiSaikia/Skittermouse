#include "test_framework.h"

#include "net/discovery_table.h"
#include "net/frame_assembler.h"
#include "net/ws_frame.h"
#include "net/ws_handshake.h"

#include <string>

using namespace sm::net;

void run_transport_tests() {
    // --- WebSocket accept key (RFC 6455 example) ----------------------------
    {
        SM_CHECK_EQ(wsAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="),
                    std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

        std::string k = wsGenerateClientKey(); // base64(16 bytes) = 24 chars, "==" tail
        SM_CHECK_EQ(k.size(), 24u);
        SM_CHECK_EQ(k.substr(22), std::string("=="));

        std::string req = wsBuildClientHandshake("host:9999", "/input", k);
        SM_CHECK(req.find("GET /input HTTP/1.1") != std::string::npos);
        SM_CHECK(req.find("Upgrade: websocket") != std::string::npos);
        SM_CHECK(req.find(k) != std::string::npos);

        std::string resp = wsBuildServerResponse("dGhlIHNhbXBsZSBub25jZQ==");
        SM_CHECK(resp.find("101 Switching Protocols") != std::string::npos);
        SM_CHECK(resp.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    }

    // --- Discovery table: last-seen + staleness (spec 6) --------------------
    {
        DiscoveryTable dt;
        Beacon b1;
        b1.machine_id = "id1";
        b1.machine_name = "A";
        Beacon b2;
        b2.machine_id = "id2";
        b2.machine_name = "B";

        dt.onBeacon(b1, 1000);
        dt.onBeacon(b2, 1000);
        SM_CHECK_EQ(dt.size(), 2u);
        SM_CHECK_EQ(dt.live(1500, 1000).size(), 2u); // both fresh
        SM_CHECK_EQ(dt.live(2001, 1000).size(), 0u); // both stale

        dt.onBeacon(b1, 2001);                        // refresh id1
        SM_CHECK_EQ(dt.live(2500, 1000).size(), 1u);
        SM_CHECK_EQ(dt.size(), 2u);                   // same id updates in place

        dt.purge(2500, 1000);                         // id2 (last seen 1000) dropped
        SM_CHECK_EQ(dt.size(), 1u);
    }

    // --- Frame assembler: stream reassembly (spec 5.1) ----------------------
    {
        Bytes p1(5, 0xAA), p2(200, 0xBB);
        Bytes f1 = wsEncodeFrame(WsOpcode::Binary, p1.data(), p1.size(), true);
        Bytes f2 = wsEncodeFrame(WsOpcode::Binary, p2.data(), p2.size(), true);

        WsFrameAssembler fa;
        Bytes both;
        both.insert(both.end(), f1.begin(), f1.end());
        both.insert(both.end(), f2.begin(), f2.end());
        fa.feed(both.data(), both.size());

        WsFrame out;
        SM_CHECK(fa.next(out));
        SM_CHECK(out.payload == p1);
        SM_CHECK(fa.next(out));
        SM_CHECK(out.payload == p2);
        SM_CHECK(!fa.next(out));

        // A frame split across two feeds is delivered once fully buffered.
        WsFrameAssembler fa2;
        Bytes f3 = wsEncodeFrame(WsOpcode::Text, p1.data(), p1.size(), false);
        fa2.feed(f3.data(), 2); // partial header
        SM_CHECK(!fa2.next(out));
        fa2.feed(f3.data() + 2, f3.size() - 2); // remainder
        SM_CHECK(fa2.next(out));
        SM_CHECK(out.payload == p1);
    }
}
