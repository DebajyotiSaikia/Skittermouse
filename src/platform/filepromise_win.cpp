// Windows delay-rendered file promise (spec 9.1). A custom IDataObject advertises
// CFSTR_FILEDESCRIPTORW (names/sizes, cheap, produced immediately) plus a promised
// CFSTR_FILECONTENTS per file, each backed by a read-only IStream. Real bytes are
// pulled through the injected FileByteSource only when a paste/drop target actually
// calls IStream::Read -- the exact moment, and only then, the network file channel is
// driven (spec 9). Because the target believes it is reading a slow stream (identical
// to copying from a network share), Explorer shows its OWN native copy-progress and
// error UI, so there is nothing custom to build (spec 9.1). No staging temp file: data
// flows paste-request -> source callback -> target, with no intermediate copy.
//
// Native OLE only (ole32/shell32), zero third-party. The COM surface here is exactly
// what Explorer exercises, so filepromise_win_tests drives it end to end (GetData +
// IStream::Read, including the mid-stream error path) with an in-memory source -- no
// Explorer and no sockets needed.

#include "platform/filepromise_win.h"

#include <windows.h>

#include <objidl.h>
#include <shlobj.h>

#include <new>
#include <string>
#include <vector>

namespace sm::platform {

namespace {

// Registered (delayed) clipboard formats. Registered once; the ids are process-wide.
UINT cfDescriptor() {
    static UINT id = RegisterClipboardFormatA(CFSTR_FILEDESCRIPTORW);
    return id;
}
UINT cfContents() {
    static UINT id = RegisterClipboardFormatA(CFSTR_FILECONTENTS);
    return id;
}
UINT cfPreferredEffect() {
    static UINT id = RegisterClipboardFormatA(CFSTR_PREFERREDDROPEFFECT);
    return id;
}

std::wstring toWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// --- Read-only stream over one promised file -------------------------------------
// Explorer calls Stat (for the size), then Read (repeatedly) and sometimes Seek. The
// FileByteSource may block until bytes are available (network pull) and returns -1 on
// error, which becomes STG_E_READFAULT so Explorer surfaces its own error UI.
class PromiseStream : public IStream {
public:
    PromiseStream(FileByteSource source, uint32_t index, uint64_t size, std::wstring name)
        : source_(std::move(source)), index_(index), size_(size), name_(std::move(name)) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream) {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    // ISequentialStream
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        if (!pv) return STG_E_INVALIDPOINTER;
        ULONG total = 0;
        auto* out = static_cast<uint8_t*>(pv);
        while (total < cb) {
            int n = source_(index_, pos_, out + total, cb - total);
            if (n < 0) {
                if (pcbRead) *pcbRead = total;
                return STG_E_READFAULT; // mid-stream failure -> Explorer's native error UI
            }
            if (n == 0) break; // clean EOF
            pos_ += static_cast<uint64_t>(n);
            total += static_cast<ULONG>(n);
        }
        if (pcbRead) *pcbRead = total;
        return S_OK; // S_OK even at EOF with total < cb (per IStream contract)
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override {
        return STG_E_ACCESSDENIED; // read-only promise
    }

    // IStream
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin,
                                   ULARGE_INTEGER* newPos) override {
        int64_t base = 0;
        switch (origin) {
            case STREAM_SEEK_SET: base = 0; break;
            case STREAM_SEEK_CUR: base = static_cast<int64_t>(pos_); break;
            case STREAM_SEEK_END: base = static_cast<int64_t>(size_); break;
            default: return STG_E_INVALIDFUNCTION;
        }
        int64_t np = base + move.QuadPart;
        if (np < 0) return STG_E_INVALIDFUNCTION;
        pos_ = static_cast<uint64_t>(np);
        if (newPos) newPos->QuadPart = pos_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* dst, ULARGE_INTEGER cb, ULARGE_INTEGER* read,
                                     ULARGE_INTEGER* written) override {
        if (!dst) return STG_E_INVALIDPOINTER;
        uint8_t buf[65536];
        uint64_t rd = 0, wr = 0;
        uint64_t remaining = cb.QuadPart;
        while (remaining > 0) {
            ULONG want = static_cast<ULONG>(remaining < sizeof(buf) ? remaining : sizeof(buf));
            ULONG got = 0;
            HRESULT hr = Read(buf, want, &got);
            if (FAILED(hr)) return hr;
            if (got == 0) break;
            rd += got;
            ULONG w = 0;
            hr = dst->Write(buf, got, &w);
            if (FAILED(hr)) return hr;
            wr += w;
            remaining -= got;
        }
        if (read) read->QuadPart = rd;
        if (written) written->QuadPart = wr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD flag) override {
        if (!stat) return STG_E_INVALIDPOINTER;
        ZeroMemory(stat, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = size_;
        stat->grfMode = STGM_READ;
        if (flag != STATFLAG_NONAME && !name_.empty()) {
            std::size_t bytes = (name_.size() + 1) * sizeof(wchar_t);
            stat->pwcsName = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
            if (stat->pwcsName) memcpy(stat->pwcsName, name_.c_str(), bytes);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** out) override {
        if (!out) return STG_E_INVALIDPOINTER;
        auto* c = new (std::nothrow) PromiseStream(source_, index_, size_, name_);
        if (!c) return E_OUTOFMEMORY;
        c->pos_ = pos_;
        *out = c;
        return S_OK;
    }

private:
    LONG ref_ = 1;
    FileByteSource source_;
    uint32_t index_ = 0;
    uint64_t size_ = 0;
    uint64_t pos_ = 0;
    std::wstring name_;
};

// --- Format enumerator ------------------------------------------------------------
class FormatEnum : public IEnumFORMATETC {
public:
    explicit FormatEnum(std::vector<FORMATETC> formats) : formats_(std::move(formats)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* out, ULONG* fetched) override {
        ULONG n = 0;
        while (n < celt && index_ < formats_.size()) out[n++] = formats_[index_++];
        if (fetched) *fetched = n;
        return n == celt ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override {
        index_ += celt;
        return index_ <= formats_.size() ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        index_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        auto* c = new (std::nothrow) FormatEnum(formats_);
        if (!c) return E_OUTOFMEMORY;
        c->index_ = index_;
        *out = c;
        return S_OK;
    }

private:
    LONG ref_ = 1;
    std::vector<FORMATETC> formats_;
    std::size_t index_ = 0;
};

// --- The promised-files data object ----------------------------------------------
class FilePromiseData : public IDataObject {
public:
    FilePromiseData(std::vector<PromisedFile> files, FileByteSource source)
        : files_(std::move(files)), source_(std::move(source)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fmt, STGMEDIUM* med) override {
        if (!fmt || !med) return E_INVALIDARG;
        ZeroMemory(med, sizeof(*med));

        if (fmt->cfFormat == cfDescriptor() && (fmt->tymed & TYMED_HGLOBAL)) {
            HGLOBAL h = buildDescriptor();
            if (!h) return E_OUTOFMEMORY;
            med->tymed = TYMED_HGLOBAL;
            med->hGlobal = h;
            return S_OK;
        }
        if (fmt->cfFormat == cfContents() && (fmt->tymed & TYMED_ISTREAM)) {
            const LONG i = fmt->lindex < 0 ? 0 : fmt->lindex;
            if (static_cast<std::size_t>(i) >= files_.size()) return DV_E_LINDEX;
            med->tymed = TYMED_ISTREAM;
            med->pstm = new (std::nothrow) PromiseStream(source_, static_cast<uint32_t>(i),
                                                         files_[i].size, toWide(files_[i].name));
            return med->pstm ? S_OK : E_OUTOFMEMORY;
        }
        if (fmt->cfFormat == cfPreferredEffect() && (fmt->tymed & TYMED_HGLOBAL)) {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (!h) return E_OUTOFMEMORY;
            if (DWORD* p = static_cast<DWORD*>(GlobalLock(h))) {
                *p = DROPEFFECT_COPY;
                GlobalUnlock(h);
            }
            med->tymed = TYMED_HGLOBAL;
            med->hGlobal = h;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fmt) override {
        if (!fmt) return E_INVALIDARG;
        if (fmt->cfFormat == cfDescriptor() && (fmt->tymed & TYMED_HGLOBAL)) return S_OK;
        if (fmt->cfFormat == cfContents() && (fmt->tymed & TYMED_ISTREAM)) return S_OK;
        if (fmt->cfFormat == cfPreferredEffect() && (fmt->tymed & TYMED_HGLOBAL)) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        if (!out) return E_POINTER;
        auto fmt = [](UINT cf, LONG lindex, DWORD tymed) {
            FORMATETC f{};
            f.cfFormat = static_cast<CLIPFORMAT>(cf);
            f.ptd = nullptr;
            f.dwAspect = DVASPECT_CONTENT;
            f.lindex = lindex;
            f.tymed = tymed;
            return f;
        };
        std::vector<FORMATETC> formats = {fmt(cfDescriptor(), -1, TYMED_HGLOBAL),
                                          fmt(cfContents(), -1, TYMED_ISTREAM),
                                          fmt(cfPreferredEffect(), -1, TYMED_HGLOBAL)};
        *out = new (std::nothrow) FormatEnum(std::move(formats));
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    // FILEGROUPDESCRIPTORW: names + sizes, so Explorer can show them and size the
    // progress bar before any byte is pulled (spec 9.1).
    HGLOBAL buildDescriptor() const {
        const std::size_t n = files_.size();
        const std::size_t bytes = sizeof(UINT) + n * sizeof(FILEDESCRIPTORW);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!h) return nullptr;
        auto* g = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(h));
        if (!g) {
            GlobalFree(h);
            return nullptr;
        }
        g->cItems = static_cast<UINT>(n);
        for (std::size_t i = 0; i < n; ++i) {
            FILEDESCRIPTORW& fd = g->fgd[i];
            fd.dwFlags = FD_FILESIZE | FD_PROGRESSUI | FD_UNICODE;
            fd.nFileSizeHigh = static_cast<DWORD>(files_[i].size >> 32);
            fd.nFileSizeLow = static_cast<DWORD>(files_[i].size & 0xFFFFFFFFu);
            std::wstring w = toWide(files_[i].name);
            if (w.size() >= MAX_PATH) w.resize(MAX_PATH - 1);
            memcpy(fd.cFileName, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        }
        GlobalUnlock(h);
        return h;
    }

    LONG ref_ = 1;
    std::vector<PromisedFile> files_;
    FileByteSource source_;
};

} // namespace

IDataObject* createFilePromiseDataObject(const std::vector<PromisedFile>& files,
                                         FileByteSource source) {
    if (files.empty() || !source) return nullptr;
    return new (std::nothrow) FilePromiseData(files, std::move(source));
}

bool putFilePromiseOnClipboard(const std::vector<PromisedFile>& files, FileByteSource source) {
    IDataObject* obj = createFilePromiseDataObject(files, std::move(source));
    if (!obj) return false;
    HRESULT hr = OleSetClipboard(obj); // requires OleInitialize on this thread
    obj->Release();                    // the clipboard holds its own reference
    return SUCCEEDED(hr);
}

} // namespace sm::platform
