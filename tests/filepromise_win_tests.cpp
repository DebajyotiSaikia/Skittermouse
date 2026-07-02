#include "test_framework.h"

#ifdef _WIN32

#include "platform/filepromise_win.h"

#include <windows.h>

#include <objidl.h>
#include <shlobj.h>

#include <cstring>
#include <vector>

using namespace sm::platform;

namespace {

std::vector<uint8_t> blob(std::size_t n, uint8_t seed) {
    std::vector<uint8_t> b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>((i * 31u + seed) & 0xFF);
    return b;
}

// Read the whole promised stream for file `i` in small chunks (exercises multi-Read +
// offset advance, exactly as Explorer does over a slow source).
std::vector<uint8_t> readContents(IDataObject* obj, LONG i) {
    FORMATETC fe{};
    fe.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatA(CFSTR_FILECONTENTS));
    fe.dwAspect = DVASPECT_CONTENT;
    fe.lindex = i;
    fe.tymed = TYMED_ISTREAM;
    STGMEDIUM med{};
    std::vector<uint8_t> got;
    if (obj->GetData(&fe, &med) != S_OK || !med.pstm) return got;
    uint8_t buf[7];
    for (;;) {
        ULONG r = 0;
        if (med.pstm->Read(buf, sizeof(buf), &r) != S_OK) break;
        if (r == 0) break;
        got.insert(got.end(), buf, buf + r);
    }
    ReleaseStgMedium(&med);
    return got;
}

} // namespace

// Drives the delay-render IDataObject exactly as Explorer would (descriptor query +
// per-file IStream::Read + the mid-stream error path), with an in-memory byte source
// -- so the whole COM surface is validated headlessly, no Explorer, no sockets.
void run_filepromise_win_tests() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const std::vector<uint8_t> f0 = blob(1000, 3);
    const std::vector<uint8_t> f1 = blob(37, 9); // small, non-chunk-aligned
    std::vector<std::vector<uint8_t>> data = {f0, f1};

    FileByteSource source = [&data](uint32_t index, uint64_t offset, uint8_t* buf,
                                    uint32_t cap) -> int {
        if (index >= data.size()) return -1;
        const std::vector<uint8_t>& d = data[index];
        if (offset >= d.size()) return 0; // EOF
        uint32_t n = static_cast<uint32_t>(d.size() - offset);
        if (n > cap) n = cap;
        std::memcpy(buf, d.data() + offset, n);
        return static_cast<int>(n);
    };

    std::vector<PromisedFile> files = {{"alpha.bin", f0.size()}, {"beta.bin", f1.size()}};
    IDataObject* obj = createFilePromiseDataObject(files, source);
    SM_CHECK(obj != nullptr);
    if (!obj) return;

    // 1. FILEGROUPDESCRIPTORW carries the right count, names and sizes up-front.
    FORMATETC fd{};
    fd.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatA(CFSTR_FILEDESCRIPTORW));
    fd.dwAspect = DVASPECT_CONTENT;
    fd.lindex = -1;
    fd.tymed = TYMED_HGLOBAL;
    STGMEDIUM smd{};
    SM_CHECK(obj->GetData(&fd, &smd) == S_OK);
    auto* g = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(smd.hGlobal));
    SM_CHECK(g != nullptr);
    if (g) {
        SM_CHECK_EQ(static_cast<int>(g->cItems), 2);
        SM_CHECK(std::wcscmp(g->fgd[0].cFileName, L"alpha.bin") == 0);
        SM_CHECK(std::wcscmp(g->fgd[1].cFileName, L"beta.bin") == 0);
        SM_CHECK_EQ(static_cast<int>(g->fgd[0].nFileSizeLow), static_cast<int>(f0.size()));
        SM_CHECK_EQ(static_cast<int>(g->fgd[1].nFileSizeLow), static_cast<int>(f1.size()));
        GlobalUnlock(smd.hGlobal);
    }
    ReleaseStgMedium(&smd);

    // 2. Each promised stream yields the exact bytes, pulled on demand in small reads.
    std::vector<uint8_t> got0 = readContents(obj, 0);
    std::vector<uint8_t> got1 = readContents(obj, 1);
    SM_CHECK(got0 == f0);
    SM_CHECK(got1 == f1);

    // 3. QueryGetData accepts the advertised formats and rejects an unknown one.
    SM_CHECK(obj->QueryGetData(&fd) == S_OK);
    FORMATETC bogus = fd;
    bogus.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"SkittermouseNoSuchFormat"));
    SM_CHECK(obj->QueryGetData(&bogus) == DV_E_FORMATETC);

    obj->Release();

    // 4. Mid-stream error: a source that fails partway makes IStream::Read return a
    //    failure HRESULT (spec 9.1: Explorer shows its own native error UI).
    FileByteSource failing = [](uint32_t, uint64_t offset, uint8_t* buf, uint32_t cap) -> int {
        if (offset >= 10) return -1; // hard error after 10 bytes
        uint32_t n = cap < static_cast<uint32_t>(10 - offset) ? cap
                                                              : static_cast<uint32_t>(10 - offset);
        std::memset(buf, 0xAB, n);
        return static_cast<int>(n);
    };
    std::vector<PromisedFile> one = {{"broken.bin", 100}};
    IDataObject* obj2 = createFilePromiseDataObject(one, failing);
    SM_CHECK(obj2 != nullptr);
    if (obj2) {
        FORMATETC fe{};
        fe.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatA(CFSTR_FILECONTENTS));
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex = 0;
        fe.tymed = TYMED_ISTREAM;
        STGMEDIUM med{};
        SM_CHECK(obj2->GetData(&fe, &med) == S_OK);
        if (med.pstm) {
            uint8_t buf[100];
            ULONG r = 0;
            HRESULT hr = med.pstm->Read(buf, sizeof(buf), &r);
            SM_CHECK(FAILED(hr));           // surfaced as a stream read fault
            SM_CHECK_EQ(static_cast<int>(r), 10); // the 10 good bytes came through first
        }
        ReleaseStgMedium(&med);
        obj2->Release();
    }

    CoUninitialize();
}

#else
void run_filepromise_win_tests() {} // non-Windows: the delay-render provider is macOS-side
#endif
