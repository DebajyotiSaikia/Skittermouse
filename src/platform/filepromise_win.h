#pragma once

// Windows-only factory for the delay-render IDataObject (spec 9.1), split out so the
// destination-side paste path and the unit tests can build the object WITHOUT putting
// it on the clipboard. Everything COM stays in filepromise_win.cpp.

#ifdef _WIN32

#include "platform/filepromise.h"

struct IDataObject;

namespace sm::platform {

// Build the promised-files IDataObject (CFSTR_FILEDESCRIPTORW + delay-rendered
// CFSTR_FILECONTENTS backed by `source`). The caller owns one reference and must
// Release() it. COM/OLE must be initialised on the calling thread. Null on failure.
IDataObject* createFilePromiseDataObject(const std::vector<PromisedFile>& files,
                                         FileByteSource source);

} // namespace sm::platform

#endif // _WIN32
