#pragma once

// Delay-rendered / promised file transfer glue (spec 9). The OS-native mechanism that
// makes "paste on the destination" trigger the actual byte pull, with the OS's own
// copy-progress UI (Explorer / Finder) appearing for free -- the same trick Outlook
// uses to drag an unsaved attachment. The wire path (net/FileChannel + file_session)
// is already done + validated; this is only the OS delay-render provider on top.
//
// The byte source is injected as a plain callback so the provider is decoupled from
// where bytes come from: on the SOURCE machine it reads local disk files; on the
// DESTINATION it drives a net::FileReceiver pulling over the on-demand /files channel.
// That indirection also lets the provider be unit-tested with an in-memory source,
// with no Explorer/Finder and no sockets.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sm::platform {

struct PromisedFile {
    std::string name;   // display file name (no path)
    uint64_t size = 0;  // total bytes (advertised up-front for the progress UI)
};

// Pull up to `cap` bytes of file `index` starting at `offset` into `buf`. Returns the
// number of bytes produced (0 = clean EOF), or -1 on error -- which the provider turns
// into a native IStream/promise read failure so Explorer/Finder shows its own error UI
// (spec 9.1: no custom error UI to build).
using FileByteSource =
    std::function<int(uint32_t index, uint64_t offset, uint8_t* buf, uint32_t cap)>;

// Put a delay-rendered multi-file promise on the clipboard (spec 9.1/9.2). Advertises
// the file metadata immediately; real bytes are pulled through `source` only when a
// paste/drop actually reads the contents. Returns false on failure. Implemented per
// OS (Windows IDataObject / macOS NSFilePromiseProvider).
bool putFilePromiseOnClipboard(const std::vector<PromisedFile>& files, FileByteSource source);

} // namespace sm::platform
