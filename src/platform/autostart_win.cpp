// Windows auto-start via Task Scheduler, elevated (spec 13). Drives schtasks.exe
// (the native Task Scheduler CLI) so the login task runs with highest privileges,
// avoiding UIPI injection failures. Native Win32, zero third-party.

#include "platform/autostart.h"

#include <windows.h>

#include <string>
#include <vector>

namespace sm::platform {

namespace {

const wchar_t* kTaskName = L"Skittermouse";

std::wstring exePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

// Run schtasks.exe with args, hidden, and return its exit code (-1 on launch fail).
int runSchtasks(const std::wstring& args) {
    std::wstring cmd = L"schtasks.exe " + args;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

} // namespace

bool enableAutostart() {
    std::wstring args = L"/Create /TN " + std::wstring(kTaskName) + L" /TR \"" + exePath() +
                        L"\" /SC ONLOGON /RL HIGHEST /F";
    return runSchtasks(args) == 0;
}

bool disableAutostart() {
    return runSchtasks(L"/Delete /TN " + std::wstring(kTaskName) + L" /F") == 0;
}

bool isAutostartEnabled() {
    return runSchtasks(L"/Query /TN " + std::wstring(kTaskName)) == 0;
}

} // namespace sm::platform
