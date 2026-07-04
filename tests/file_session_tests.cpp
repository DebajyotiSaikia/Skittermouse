#include "test_framework.h"

#include "net/file_session.h"

#include <string>

using namespace sm::net;

void run_file_session_tests() {
    // --- Single file, multi-chunk round-trip --------------------------------
    {
        FileSender s;
        Bytes data(1000);
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i % 251);
        s.addFile("a.bin", data);

        FileReceiver r;
        Bytes meta = s.meta();
        SM_CHECK(r.acceptMeta(meta.data(), meta.size()));
        SM_CHECK_EQ(r.fileCount(), 1u);
        SM_CHECK_EQ(r.entry(0).name, std::string("a.bin"));
        SM_CHECK_EQ(r.entry(0).size, 1000ULL);

        for (;;) {
            Bytes c = s.nextChunk(0, 256);
            if (c.empty()) break;
            SM_CHECK(r.acceptChunk(c.data(), c.size()));
        }
        SM_CHECK(s.complete(0));
        SM_CHECK(r.complete(0));
        SM_CHECK(r.data(0) == data);
    }

    // --- Multiple files + allComplete ---------------------------------------
    {
        FileSender s;
        Bytes d1 = {1, 2, 3};
        Bytes d2(500, 0xAB);
        s.addFile("one", d1);
        s.addFile("two", d2);

        FileReceiver r;
        Bytes meta = s.meta();
        SM_CHECK(r.acceptMeta(meta.data(), meta.size()));
        SM_CHECK_EQ(r.fileCount(), 2u);

        for (uint32_t idx = 0; idx < 2; ++idx) {
            for (;;) {
                Bytes c = s.nextChunk(idx, 128);
                if (c.empty()) break;
                SM_CHECK(r.acceptChunk(c.data(), c.size()));
            }
        }
        SM_CHECK(r.allComplete());
        SM_CHECK(r.data(0) == d1);
        SM_CHECK(r.data(1) == d2);
    }

    // --- received() tracks contiguous arrived bytes, not buffer capacity -----
    // Regression: the destination pull must serve only bytes that have actually
    // arrived. data(i).size() is the FULL pre-allocated length from the moment meta
    // is accepted, so the pull loop must use received(i) instead -- otherwise it hands
    // Explorer zero-filled tails and, when the source closes its linger, fails the
    // paste with a bogus "A disk error occurred during a read operation".
    {
        FileSender s;
        Bytes data(1000);
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i % 251);
        s.addFile("big.bin", data);

        FileReceiver r;
        Bytes meta = s.meta();
        SM_CHECK(r.acceptMeta(meta.data(), meta.size()));
        SM_CHECK_EQ(r.data(0).size(), 1000u); // buffer pre-sized to full length...
        SM_CHECK_EQ(r.received(0), 0u);       // ...but nothing received yet
        SM_CHECK(!r.complete(0));

        Bytes c1 = s.nextChunk(0, 256);
        SM_CHECK(r.acceptChunk(c1.data(), c1.size()));
        SM_CHECK_EQ(r.received(0), 256u); // only the first chunk is present
        SM_CHECK(!r.complete(0));

        for (;;) {
            Bytes c = s.nextChunk(0, 256);
            if (c.empty()) break;
            SM_CHECK(r.acceptChunk(c.data(), c.size()));
        }
        SM_CHECK_EQ(r.received(0), 1000u);
        SM_CHECK(r.complete(0));
        SM_CHECK_EQ(r.received(99), 0u); // out-of-range index is safe
    }

    // --- Malformed / out-of-bounds chunks are rejected ----------------------
    {
        FileSender s;
        s.addFile("x", Bytes{1, 2, 3, 4});
        FileReceiver r;
        Bytes meta = s.meta();
        r.acceptMeta(meta.data(), meta.size());

        Bytes oob = encodeFileChunk(0, 100, reinterpret_cast<const uint8_t*>("z"), 1);
        SM_CHECK(!r.acceptChunk(oob.data(), oob.size())); // offset past end
        Bytes badIdx = encodeFileChunk(5, 0, reinterpret_cast<const uint8_t*>("z"), 1);
        SM_CHECK(!r.acceptChunk(badIdx.data(), badIdx.size())); // unknown file index
    }
}
