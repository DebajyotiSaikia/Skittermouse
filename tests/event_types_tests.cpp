#include "test_framework.h"

#include "core/event_types.h"

#include <cstddef>

using namespace sm::core;

void run_event_types_tests() {
    // Wire-size guarantees: both machines must agree byte-for-byte.
    SM_CHECK_EQ(sizeof(InputEvent), 12u);
    SM_CHECK_EQ(sizeof(VarHeader), 6u);

    // Field offsets pin the exact on-wire layout (packed, no padding).
    SM_CHECK_EQ(offsetof(InputEvent, protocol_version), 0u);
    SM_CHECK_EQ(offsetof(InputEvent, type), 1u);
    SM_CHECK_EQ(offsetof(InputEvent, dx), 2u);
    SM_CHECK_EQ(offsetof(InputEvent, dy), 4u);
    SM_CHECK_EQ(offsetof(InputEvent, code), 6u);
    SM_CHECK_EQ(offsetof(InputEvent, down), 7u);
    SM_CHECK_EQ(offsetof(InputEvent, timestamp_ms), 8u);
    SM_CHECK_EQ(offsetof(VarHeader, protocol_version), 0u);
    SM_CHECK_EQ(offsetof(VarHeader, type), 1u);
    SM_CHECK_EQ(offsetof(VarHeader, payload_length), 2u);

    SM_CHECK_EQ(static_cast<int>(kProtocolVersion), 1);

    // MessageType tag values are part of the wire contract -- pin them so an
    // accidental reorder can't silently change the protocol.
    SM_CHECK_EQ(static_cast<int>(MessageType::MouseMove), 1);
    SM_CHECK_EQ(static_cast<int>(MessageType::MouseButton), 2);
    SM_CHECK_EQ(static_cast<int>(MessageType::KeyEvent), 3);
    SM_CHECK_EQ(static_cast<int>(MessageType::SwitchOwner), 4);
    SM_CHECK_EQ(static_cast<int>(MessageType::Heartbeat), 5);
    SM_CHECK_EQ(static_cast<int>(MessageType::ClipboardUpdate), 6);
    SM_CHECK_EQ(static_cast<int>(MessageType::FilePromiseMeta), 7);
    SM_CHECK_EQ(static_cast<int>(MessageType::FileChunk), 8);
    SM_CHECK_EQ(static_cast<int>(MessageType::PairingMsg), 9);
}
