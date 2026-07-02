#include "test_framework.h"

#include "core/config.h"
#include "core/hotkey.h"
#include "net/file_transfer.h"
#include "ui/menu_model.h"

#include <string>
#include <vector>

using namespace sm;

void run_feature_tests() {
    // --- Hotkey parsing (spec 4.1) ------------------------------------------
    {
        auto h = core::parseHotkey("Ctrl+Alt+Space");
        SM_CHECK(h.valid);
        SM_CHECK_EQ(h.modifiers, core::hotkey_mod::Control | core::hotkey_mod::Alt);
        SM_CHECK_EQ(h.key, 0x20);

        auto h2 = core::parseHotkey("ctrl+shift+alt+space"); // case-insensitive
        SM_CHECK(h2.valid);
        SM_CHECK_EQ(h2.modifiers,
                    core::hotkey_mod::Control | core::hotkey_mod::Shift | core::hotkey_mod::Alt);

        auto f5 = core::parseHotkey("F5");
        SM_CHECK(f5.valid);
        SM_CHECK_EQ(f5.modifiers, 0u);
        SM_CHECK_EQ(f5.key, 0x74); // VK_F5

        auto wl = core::parseHotkey("Win+L");
        SM_CHECK(wl.valid);
        SM_CHECK_EQ(wl.modifiers, core::hotkey_mod::Win);
        SM_CHECK_EQ(wl.key, static_cast<uint16_t>('L'));

        SM_CHECK(!core::parseHotkey("Ctrl+").valid);          // trailing '+'
        SM_CHECK(!core::parseHotkey("Ctrl+Alt+Nope").valid);  // unknown key
        SM_CHECK(!core::parseHotkey("").valid);               // empty
        SM_CHECK(!core::parseHotkey("A+B").valid);            // two keys
        SM_CHECK(!core::parseHotkey("Ctrl+Shift").valid);     // modifiers only, no key
    }

    // --- Menu model: owner marked, offline greyed-not-omitted (spec 10/4.3) --
    {
        std::vector<core::PairedDevice> devs;
        core::PairedDevice a;
        a.id = "self";
        a.name = "Me";
        core::PairedDevice b;
        b.id = "peer1";
        b.name = "Desk";
        core::PairedDevice c;
        c.id = "peer2";
        c.name = "Lap";
        devs.push_back(a);
        devs.push_back(b);
        devs.push_back(c);

        auto items = ui::buildMachineMenu(devs, "self", "peer1", {"peer1"});
        SM_CHECK_EQ(items.size(), 3u);
        SM_CHECK(items[0].is_self);
        SM_CHECK(items[0].is_online); // self is always reachable to itself
        SM_CHECK(items[1].is_owner);
        SM_CHECK(items[1].is_online);
        SM_CHECK(!items[2].is_online); // peer2 offline but still listed (greyed)
        SM_CHECK(!items[2].is_owner);
    }

    // --- File-transfer metadata + chunk codecs (spec 9) ---------------------
    {
        std::vector<net::FileEntry> files = {{"a.txt", 100}, {"big name.bin", 4294967296ULL}};
        auto enc = net::encodeFilePromiseMeta(files);
        std::vector<net::FileEntry> dec;
        SM_CHECK(net::decodeFilePromiseMeta(enc.data(), enc.size(), dec));
        SM_CHECK_EQ(dec.size(), 2u);
        SM_CHECK_EQ(dec[0].name, std::string("a.txt"));
        SM_CHECK_EQ(dec[0].size, 100ULL);
        SM_CHECK_EQ(dec[1].name, std::string("big name.bin"));
        SM_CHECK_EQ(dec[1].size, 4294967296ULL); // > 4GB survives (64-bit size)

        std::vector<net::FileEntry> bad;
        SM_CHECK(!net::decodeFilePromiseMeta(enc.data(), 3, bad)); // truncated header

        uint8_t data[] = {1, 2, 3, 4, 5};
        auto ch = net::encodeFileChunk(7, 1000, data, 5);
        uint32_t idx = 0;
        uint64_t off = 0;
        net::Bytes chunk;
        SM_CHECK(net::decodeFileChunk(ch.data(), ch.size(), idx, off, chunk));
        SM_CHECK_EQ(idx, 7u);
        SM_CHECK_EQ(off, 1000ULL);
        SM_CHECK_EQ(chunk.size(), 5u);
        SM_CHECK_EQ(chunk[4], 5);
        SM_CHECK(!net::decodeFileChunk(ch.data(), 5, idx, off, chunk)); // too short for header
    }
}
