// tio.exe: the program of the plugin ra9fael.tio.
//
// The same split as ra9fael.qr and WinShun's own winshun.capture: the window is
// built on Qt, and WinShun has Qt where it is installed, so this starter —
// without Qt and with the C runtime linked in — adds WinShun's Qt folder to the
// folders DLLs are looked for in and loads TioApp.dll from its own folder: that
// one imports Qt, which the loader then finds there. Nothing else is searched (no
// PATH, no current folder): the application's folder, System32 and WinShun's.
//
// Which folder: the host's shared-Qt contract declares `"qt": "host"` in
// plugin.json and sets WS_QT_DIR (WinShun 0.22.0 and later); WS_APP_DIR is the
// older way (the host's own folder) and stays as the fallback.
//
// Two other entries avoid the DLL, because neither should depend on the machine's
// Qt being loadable:
//   * `tio.exe --probe ports|backends|verify <folder>`: the read-only diagnostics
//     the native tests and tools/package.py call (probe.h);
//   * `tio.exe --serve`: the protocol without a window, which is what a headless
//     check wants.
// `tio.exe --window [--app-dir <folder>] [--en] [--dark]` opens the window
// directly (no WinShun, no protocol): how the window is checked.
#include "../../probe.h"
#include "../../plugin.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Entry = int(__cdecl*)(const wchar_t* appDir);

int fail(int code, const char* what, DWORD error = 0)
{
    // stderr goes to plugins.log; stdout is the protocol's alone.
    if (error)
        std::fprintf(stderr, "tio: %s (error %lu)\n", what, error);
    else
        std::fprintf(stderr, "tio: %s\n", what);
    std::fflush(stderr);
    return code;
}

std::wstring folderOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring environment(const wchar_t* name)
{
    std::wstring value(32768, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(name, value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
        return {};
    value.resize(length);
    return value;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool window = false;
    std::wstring appDir;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        window = window || arg == L"--window" || arg == L"--en" || arg == L"--dark";
        if (arg == L"--app-dir" && i + 1 < argc)
            appDir = argv[i + 1];
    }

    // The probe and the headless protocol stay in this process: a check must not
    // fail because the machine's Qt is another version, and the protocol is the
    // same code the DLL runs (plugin.h).
    if (argc >= 2 && std::wstring_view(argv[1]) == L"--probe") {
        std::vector<std::string> narrow;
        narrow.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i)
            narrow.push_back(winshun::utf8(std::wstring(argv[i])));
        std::vector<char*> flat;
        flat.reserve(narrow.size());
        for (std::string& text : narrow)
            flat.push_back(text.data());
        return tioplug::runProbe(static_cast<int>(flat.size()), flat.data());
    }
    if (argc >= 2 && std::wstring_view(argv[1]) == L"--serve")
        return tioplug::runServeLoop();

    if (appDir.empty())
        appDir = environment(L"WS_QT_DIR"); // the shared-Qt contract (0.22.0+)
    if (appDir.empty())
        appDir = environment(L"WS_APP_DIR"); // older hosts: their own folder
    if (appDir.empty() || ::GetFileAttributesW((appDir + L"\\Qt6Core.dll").c_str())
            == INVALID_FILE_ATTRIBUTES) {
        // No Qt to load, but the search box does not need any: serve the protocol
        // without the window rather than refusing to start. The window's action
        // then says what is missing (window_hook.h has no hook installed).
        std::fprintf(stderr, "tio: no WinShun Qt folder (%s); "
                             "running without the standalone window\n",
            appDir.empty() ? "WS_QT_DIR and WS_APP_DIR are both unset"
                           : "no Qt6Core.dll there");
        std::fflush(stderr);
        return tioplug::runServeLoop();
    }
    // The window uses the same host runtime as a normal plugin start.
    if (!::SetEnvironmentVariableW(L"WS_APP_DIR", appDir.c_str()))
        return fail(3, "WS_APP_DIR could not be passed on", ::GetLastError());

    std::wstring self(32768, L'\0');
    const DWORD selfLength = ::GetModuleFileNameW(nullptr, self.data(),
        static_cast<DWORD>(self.size()));
    if (selfLength == 0 || selfLength >= self.size())
        return fail(4, "own path unknown", ::GetLastError());
    self.resize(selfLength);

    if (!::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        || !::AddDllDirectory(appDir.c_str()))
        return fail(5, "DLL folders not set", ::GetLastError());
    const std::wstring dll = folderOf(self) + L"\\TioApp.dll";
    HMODULE module = ::LoadLibraryExW(dll.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!module)
        return fail(6, "TioApp.dll not loaded (a WinShun with another Qt?)", ::GetLastError());
    const char* name = window ? "winshunTioWindow" : "winshunTioMain";
    const auto entry = reinterpret_cast<Entry>(::GetProcAddress(module, name));
    if (!entry) {
        std::fprintf(stderr, "tio: TioApp.dll has no %s\n", name);
        std::fflush(stderr);
        return 7;
    }
    return entry(appDir.c_str());
}
