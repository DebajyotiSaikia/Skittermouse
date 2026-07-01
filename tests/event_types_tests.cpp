#include "test_framework.h"

#include "core/event_types.h"

#include <cstddef>

using namespace qq::core;

void run_event_types_tests() {
    // Wire-size guarantees: both machines must agree byte-for-byte.
    QQ_CHECK_EQ(sizeof(InputEvent), 12u);
    QQ_CHECK_EQ(sizeof(VarHeader), 6u);

    // Field offsets pin the exact on-wire layout (packed, no padding).
    QQ_CHECK_EQ(offsetof(InputEvent, protocol_version), 0u);
    QQ_CHECK_EQ(offsetof(InputEvent, type), 1u);
    QQ_CHECK_EQ(offsetof(InputEvent, dx), 2u);
    QQ_CHECK_EQ(offsetof(InputEvent, dy), 4u);
    QQ_CHECK_EQ(offsetof(InputEvent, code), 6u);
    QQ_CHECK_EQ(offsetof(InputEvent, down), 7u);
    QQ_CHECK_EQ(offsetof(InputEvent, timestamp_ms), 8u);
    QQ_CHECK_EQ(offsetof(VarHeader, protocol_version), 0u);
    QQ_CHECK_EQ(offsetof(VarHeader, type), 1u);
    QQ_CHECK_EQ(offsetof(VarHeader, payload_length), 2u);

    QQ_CHECK_EQ(static_cast<int>(kProtocolVersion), 1);

    // MessageType tag values are part of the wire contract -- pin them so an
    // accidental reorder can't silently change the protocol.
    QQ_CHECK_EQ(static_cast<int>(MessageType::MouseMove), 1);
    QQ_CHECK_EQ(static_cast<int>(MessageType::MouseButton), 2);
    QQ_CHECK_EQ(static_cast<int>(MessageType::KeyEvent), 3);
    QQ_CHECK_EQ(static_cast<int>(MessageType::SwitchOwner), 4);
    QQ_CHECK_EQ(static_cast<int>(MessageType::Heartbeat), 5);
    QQ_CHECK_EQ(static_cast<int>(MessageType::ClipboardUpdate), 6);
    QQ_CHECK_EQ(static_cast<int>(MessageType::FilePromiseMeta), 7);
    QQ_CHECK_EQ(static_cast<int>(MessageType::FileChunk), 8);
    QQ_CHECK_EQ(static_cast<int>(MessageType::PairingMsg), 9);
}
