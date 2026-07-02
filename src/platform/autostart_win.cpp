// Windows auto-start via Task Scheduler, elevated (spec 13). Drives schtasks.exe
// (the native Task Scheduler CLI) so the login task runs with highest privileges,
// avoiding UIPI injection failures. Native Win32, zero third-party.

#include "platform/autostart.h"

#include <windows.h>
#include <shellapi.h>

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

// Run schtasks.exe ELEVATED via the "runas" verb (a UAC prompt). Creating/deleting a
// "run with highest privileges" login task requires admin, so enable/disable go
// through here; the user is opting in deliberately, so the prompt is expected.
// Returns the exit code, or -1 if the user declined UAC / launch failed.
int runSchtasksElevated(const std::wstring& args) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = L"runas";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return -1;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return static_cast<int>(code);
}

} // namespace

bool enableAutostart() {
    std::wstring args = L"/Create /TN " + std::wstring(kTaskName) + L" /TR \"" + exePath() +
                        L"\" /SC ONLOGON /RL HIGHEST /F";
    return runSchtasksElevated(args) == 0;
}

bool disableAutostart() {
    return runSchtasksElevated(L"/Delete /TN " + std::wstring(kTaskName) + L" /F") == 0;
}

bool isAutostartEnabled() {
    // Querying a task does not require elevation.
    return runSchtasks(L"/Query /TN " + std::wstring(kTaskName)) == 0;
}

} // namespace sm::platform
