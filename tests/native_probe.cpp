// native_probe.cpp - checks the Win32 assumptions the launcher depends on, using
// the real packaged runtime. Read-only with respect to the machine: it starts
// tio, observes it, and stops the process it created.
//
// Each check prints one line "PASS name" or "FAIL name: why". Exit code 0 only
// when every check passed.
#include "../native.h"

#include <cstdio>
#include <string>

namespace {

int g_total = 0;
int g_failed = 0;

void report(bool ok, const char* name, const std::string& why = {})
{
    ++g_total;
    if (ok) {
        std::printf("PASS %s\n", name);
    } else {
        ++g_failed;
        std::printf("FAIL %s%s%s\n", name, why.empty() ? "" : ": ", why.c_str());
    }
}

// Whether the process created with these flags is still running after `ms`.
// An interactive tio waits for the keyboard; a non-interactive one finishes as
// soon as it has reported the connection.
struct Spawn {
    bool created = false;
    DWORD error = 0;
    DWORD pid = 0;
    HANDLE process = nullptr;
};

Spawn spawn(DWORD flags, const std::wstring& commandLine, const std::wstring& runtimeDir)
{
    Spawn out;
    std::vector<wchar_t> line(commandLine.begin(), commandLine.end());
    line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    const BOOL ok = ::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, flags, nullptr,
        runtimeDir.c_str(), &startup, &info);
    if (!ok) {
        out.error = ::GetLastError();
        return out;
    }
    out.created = true;
    out.pid = info.dwProcessId;
    out.process = info.hProcess;
    ::CloseHandle(info.hThread);
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    // An absolute plugin dir: the wt backend passes the tio path inside a command
    // line that Windows Terminal resolves against its own working directory, so a
    // relative path there would start nothing.
    std::wstring pluginDir = argc >= 2 ? winshun::wide(argv[1]) : L".";
    {
        wchar_t full[MAX_PATH * 2];
        const DWORD length = ::GetFullPathNameW(pluginDir.c_str(),
            static_cast<DWORD>(sizeof full / sizeof full[0]), full, nullptr);
        if (length != 0 && length < sizeof full / sizeof full[0])
            pluginDir.assign(full, length);
    }
    const std::wstring runtimeDir = pluginDir + L"\\runtime";
    const std::wstring exe = runtimeDir + L"\\tio.exe";

    report(!pluginDir.empty() && pluginDir[0] != L'.', "the plugin dir is absolute");

    report(tioplug::fileExists(exe), "packaged tio.exe is present");

    // 1. The manifest matches the files on disk.
    const tioplug::VerifyResult package = tioplug::verifyPackage(pluginDir);
    report(package.ok, "package verifies", package.problems.empty() ? "" : package.problems.front());
    report(package.checked >= 8, "package lists every runtime file",
        "checked " + std::to_string(package.checked));
    report(package.fileCount == package.checked, "the manifest file count matches what was checked",
        "count " + std::to_string(package.fileCount) + " checked " + std::to_string(package.checked));
    report(package.totalBytes > 0, "the manifest records the runtime size",
        "bytes " + std::to_string(package.totalBytes));
    report(!package.sourceUrl.empty() && package.sourceUrl.rfind("https://", 0) == 0,
        "the manifest records where the runtime came from", package.sourceUrl);
    report(!package.license.empty(), "the manifest records the runtime licence", package.license);

    // 1b. The third-party record travels with the package: every component
    //     names a source, and the licence texts it points at exist on disk.
    report(!package.thirdParty.empty(), "the third-party record ships with the package",
        "components " + std::to_string(package.thirdParty.size()));
    {
        bool allSourced = true;
        bool allLicencesPresent = true;
        std::string why;
        for (const tioplug::ThirdParty& component : package.thirdParty) {
            if (component.source.empty() || component.source.rfind("https://", 0) != 0) {
                allSourced = false;
                why = component.name;
                break;
            }
            for (const std::string& file : component.licenseFiles) {
                if (!tioplug::fileExists(pluginDir + L"\\" + winshun::wide(file))) {
                    allLicencesPresent = false;
                    why = file;
                    break;
                }
            }
            if (!allLicencesPresent)
                break;
        }
        report(allSourced, "every component names an https source", why);
        report(allLicencesPresent, "every licence text it links to is in the package", why);
    }

    // 1c. The machine-readable record and the human-readable one must agree:
    //     every component the window shows is named in THIRD_PARTY.md, and the
    //     document is in the package too.
    {
        const std::string document = tioplug::detail::readPluginFile(pluginDir, L"THIRD_PARTY.md");
        report(!document.empty(), "THIRD_PARTY.md ships with the package");
        bool allNamed = true;
        std::string why;
        for (const tioplug::ThirdParty& component : package.thirdParty) {
            // A component is identified by its first token: the file or the
            // product name. The rest of the line is the display name, which
            // the document words its own way.
            std::string name = component.name;
            for (const char* cut : {" · ", " (", "（", " "}) {
                const size_t at = name.find(cut);
                if (at != std::string::npos)
                    name = name.substr(0, at);
            }
            if (name.empty() || document.find(name) == std::string::npos) {
                allNamed = false;
                why = name;
                break;
            }
        }
        report(allNamed, "every component in the record is named in THIRD_PARTY.md", why);
    }

    // 2. Backend detection finds what this machine actually has.
    const tioplug::Backends found = tioplug::probeBackends();
    report(found.conhost, "conhost.exe is detected");
    std::printf("INFO wt=%d version=%s\n", found.wt ? 1 : 0, found.wtVersion.c_str());
    report(!found.wt || !found.wtVersion.empty(), "wt version is readable from the package registration");

    // 3. Port enumeration is read-only and reports a COM number plus identity.
    const std::vector<tioplug::PortInfo> ports = tioplug::enumeratePorts();
    std::printf("INFO ports=%zu\n", ports.size());
    for (const tioplug::PortInfo& info : ports)
        std::printf("INFO   COM%d %s\n", info.com, winshun::utf8(info.instanceId).c_str());
    report(!ports.empty(), "the machine reports at least one port",
        "no serial port on this machine: the enumeration itself cannot be checked here");

    // 4. A missing device must fail fast with the upstream error, not hang.
    {
        const std::wstring line = L"\"" + exe + L"\" -n /dev/ttyS20";
        const Spawn run = spawn(CREATE_NEW_CONSOLE, line, runtimeDir);
        report(run.created, "CreateProcessW can start the packaged tio",
            "error " + std::to_string(run.error));
        if (run.created) {
            const DWORD waited = ::WaitForSingleObject(run.process, 6000);
            report(waited == WAIT_OBJECT_0, "a missing device exits instead of hanging");
            DWORD code = 0;
            ::GetExitCodeProcess(run.process, &code);
            std::printf("INFO exit code for a missing device: %lu\n", code);
            report(code != 0, "a missing device is reported as a failure");
            ::CloseHandle(run.process);
        }
    }

    // 5. The flag choice decides whether tio stays interactive. Without a
    //    console of its own it prints "Non-interactive mode enabled" and quits
    //    at the first disconnect; with one it waits for the keyboard.
    if (!ports.empty()) {
        const std::wstring device = L"/dev/ttyS" + std::to_wstring(ports.front().com - 1);
        const std::wstring line = L"\"" + exe + L"\" -n " + device;

        const Spawn plain = spawn(0, line, runtimeDir);
        if (plain.created) {
            const DWORD waited = ::WaitForSingleObject(plain.process, 2500);
            std::printf("INFO without CREATE_NEW_CONSOLE the process %s\n",
                waited == WAIT_OBJECT_0 ? "exited on its own" : "is still running");
            if (waited == WAIT_OBJECT_0)
                ::CloseHandle(plain.process);
            else {
                ::TerminateProcess(plain.process, 0);
                ::CloseHandle(plain.process);
            }
            report(true, "the no-console case is observed (see the INFO line)");
        }

        const Spawn own = spawn(CREATE_NEW_CONSOLE, line, runtimeDir);
        report(own.created, "CreateProcessW with a new console starts tio",
            "error " + std::to_string(own.error));
        if (own.created) {
            const DWORD waited = ::WaitForSingleObject(own.process, 2500);
            report(waited == WAIT_TIMEOUT, "tio stays interactive when it owns a console",
                "it exited on its own, so it would not read the keyboard");
            // The launcher stops it exactly this way.
            unsigned long long created = 0;
            tioplug::processCreationTime(own.pid, created);
            const bool stopped = tioplug::stopOwned(own.pid, created, 1500);
            report(stopped, "stopOwned terminates the session it created");
            ::WaitForSingleObject(own.process, 2000);
            DWORD still = 0;
            report(!::GetExitCodeProcess(own.process, &still) || still != STILL_ACTIVE,
                "the stopped process is gone");
            ::CloseHandle(own.process);
        }
    }

    // 6. A reused PID is never touched: the creation time is the identity.
    if (!ports.empty() && found.conhost) {
        const std::wstring line = L"\"" + exe + L"\" -n /dev/ttyS21";
        Spawn run = spawn(CREATE_NEW_CONSOLE, line, runtimeDir);
        if (run.created) {
            unsigned long long created = 0;
            tioplug::processCreationTime(run.pid, created);
            report(created != 0, "the creation time of a launched process is readable");
            ::WaitForSingleObject(run.process, 4000);
            ::CloseHandle(run.process);
        }
    }

    // 7. launch() itself, through both backends, with a plan built by logic.h.
    //    This is the path the plugin uses, so the checks cover the real glue
    //    rather than a hand-written CreateProcess call.
    if (!ports.empty()) {
        const tioplug::Port port = tioplug::parsePort("COM" + std::to_string(ports.front().com));
        const tioplug::Serial serial;
        tioplug::LaunchContext context;
        context.pluginDir = pluginDir;
        context.runtimeDir = runtimeDir;
        context.workDir = pluginDir;

        // conhost: we own the process, so its creation is verifiable at once.
        const tioplug::Plan conhostPlan = tioplug::planLaunch(tioplug::Backend::Conhost, "",
            winshun::utf8(exe), port, serial, "s-probe-conhost");
        report(conhostPlan.error == tioplug::PlanError::None, "conhost plan builds");
        const tioplug::LaunchResult conhost = tioplug::launch(conhostPlan, context);
        report(conhost.ok, "conhost launch reports a running session", conhost.error);
        report(conhost.pid != 0, "conhost launch records the PID");
        report(conhost.created != 0, "conhost launch records the creation time");
        if (conhost.ok) {
            const unsigned long long created = conhost.created;
            report(tioplug::ownedProcess(tioplug::Session{}, conhost.pid, created) == false,
                "an empty session owns nothing");
            tioplug::Session session;
            tioplug::beginValidation(session);
            tioplug::beginStart(session);
            report(tioplug::markRunning(session, conhost.pid, created), "the session accepts the launch");
            report(tioplug::ownedProcess(session, conhost.pid, created), "the session owns the process");
            report(tioplug::stopOwned(conhost.pid, created, 2000), "conhost session stops");
        }

        // wt: the process we create exits at once, so the session is found by
        // parent chain and creation time instead.
        if (found.wt) {
            const tioplug::Plan wtPlan = tioplug::planLaunch(tioplug::Backend::Wt,
                winshun::utf8(tioplug::findOnPath(L"wt.exe")), winshun::utf8(exe), port, serial,
                "s-probe-wt");
            report(wtPlan.error == tioplug::PlanError::None, "wt plan builds");
            const tioplug::LaunchResult wt = tioplug::launch(wtPlan, context);
            report(wt.ok, "wt launch finds the process it handed over", wt.error);
            if (wt.ok) {
                report(wt.pid != 0, "wt launch records a PID");
                report(wt.created != 0, "wt launch records a creation time");
                const std::wstring parent = tioplug::baseName(tioplug::processName(wt.pid));
                report(tioplug::lowerWide(parent) == L"tio.exe",
                    "the process found under wt really is tio", winshun::utf8(parent));
                report(tioplug::stopOwned(wt.pid, wt.created, 2000),
                    "wt session stops through the recorded PID");
            }
        }
    }

    std::printf("%s %d checks, %d failed\n", g_failed ? "FAILED" : "PASS", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
