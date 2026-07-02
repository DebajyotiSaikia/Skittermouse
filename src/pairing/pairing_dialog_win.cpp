// Windows pairing confirmation dialog (spec 7.1). Native Win32 MessageBox.

#include "pairing/pairing_dialog.h"

#include <windows.h>

#include <string>

namespace sm::platform {

namespace {
std::wstring toWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
} // namespace

bool confirmPairingCode(const std::string& code, const std::string& peerName) {
    std::wstring msg = L"Confirm this code matches the one shown on " + toWide(peerName) +
                       L":\n\n        " + toWide(code) + L"\n\nDo the codes match?";
    int r = MessageBoxW(nullptr, msg.c_str(), L"Skittermouse pairing",
                        MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
    return r == IDYES;
}

} // namespace sm::platform
