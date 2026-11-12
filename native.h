// native.h - the Win32 side of the tio launcher (ra9fael.tio).
//
// Everything OS-dependent lives here: read-only port enumeration, package
// verification, backend probing, process launch and stop, settings storage.
// The pure rules stay in logic.h; ui/ and main.cpp only glue both onto the SDK.
//
// Threads: the protocol thread never waits on a child. Launch and stop run on
// the window's worker thread, and the answer is read back from the session
// state under a mutex.
#pragma once

#include "logic.h"
#include "winshun.h"

#include <windows.h>
#include <bcrypt.h>
#include <setupapi.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tioplug {

// ---- text helpers --------------------------------------------------------------

inline std::wstring wide(const std::string& text) { return winshun::wide(text); }
inline std::string utf8(const std::wstring& text) { return winshun::utf8(text); }

inline std::wstring lowerWide(std::wstring s)
{
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(::towlower(c));
    return s;
}

inline bool fileExists(const std::wstring& path)
{
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::wstring joinPath(const std::wstring& base, const std::wstring& name)
{
    if (base.empty())
        return name;
    if (base.back() == L'\\' || base.back() == L'/')
        return base + name;
    return base + L"\\" + name;
}

// An exe reachable through PATH (the execution-alias folder included).
inline std::wstring findOnPath(const wchar_t* exe)
{
    wchar_t buffer[MAX_PATH * 2];
    wchar_t* filePart = nullptr;
    const DWORD n = ::SearchPathW(nullptr, exe, nullptr, MAX_PATH * 2, buffer, &filePart);
    if (n == 0 || n >= MAX_PATH * 2 || !filePart)
        return {};
    return std::wstring(buffer, n);
}

inline std::wstring systemDirectory()
{
    wchar_t buffer[MAX_PATH];
    const UINT n = ::GetSystemDirectoryW(buffer, MAX_PATH);
    return n ? std::wstring(buffer, n) : std::wstring();
}

// ---- read-only port enumeration (TIO-001/010) -----------------------------------
//
// Enumeration must never open the port: opening it would take it away from
// whichever program holds it, which is exactly the race TIO-008 forbids.

// What the device itself reports: the parts that identify it across a replug
// and the parts that explain what it is. Everything here is read-only, and a
// field Windows does not give stays empty rather than being invented.
struct DeviceDetails {
    std::wstring vendorId;      // "10C4" (Silicon Labs)
    std::wstring productId;     // "EA60"
    std::wstring serialNumber;  // the device's own serial, when it has one
    std::wstring manufacturer;  // the device's manufacturer string
    std::wstring chip;          // what the chip calls itself, e.g. "CP2102 USB to UART Bridge Controller"
    std::wstring busType;       // "USB", "ACPI", "PCI" ...
    std::wstring driverVersion; // the driver that owns the port
    std::wstring location;      // where it is plugged in, e.g. "Port_#0002.Hub_#0006"
    std::wstring problem;       // Windows' own complaint, when there is one

    // True when the port's own identity can be told from another device's.
    bool distinctive() const { return !vendorId.empty() || !productId.empty(); }
};

struct PortInfo {
    int com = 0;
    std::wstring instanceId;  // stable device identity
    std::wstring description; // "Silicon Labs CP210x USB to UART Bridge (COM8)"
    DeviceDetails details;    // what the device says about itself (TIO-010)
};

namespace detail {

// One device property, read through SetupAPI's property API. A property the
// device does not have, or that the caller may not read, comes back empty:
// the caller shows what exists and says nothing about the rest.
//
// The sizing call is expected to fail: SetupDiGetDevicePropertyW answers a
// zero-sized buffer with ERROR_INSUFFICIENT_BUFFER and the size to ask for, so
// only an error other than that one (ERROR_NOT_FOUND, ERROR_ACCESS_DENIED) means
// the property is not there. Treating that first answer as a failure would drop
// every property on the machine.
inline std::wstring deviceString(HDEVINFO devices, SP_DEVINFO_DATA& data, const DEVPROPKEY& key)
{
    DWORD type = 0;
    DWORD size = 0;
    ::SetLastError(0);
    const BOOL sized = ::SetupDiGetDevicePropertyW(devices, &data, &key, &type, nullptr, 0, &size, 0);
    if (!sized && ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return {};
    if (size == 0 || (type != DEVPROP_TYPE_STRING && type != DEVPROP_TYPE_STRING_LIST))
        return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!::SetupDiGetDevicePropertyW(devices, &data, &key, &type,
            reinterpret_cast<PBYTE>(value.data()), size, nullptr, 0))
        return {};
    // The string list is NUL-separated; the first entry is the one to show.
    const std::size_t end = value.find(L'\0');
    if (end != std::wstring::npos)
        value.resize(end);
    return value;
}

inline std::wstring deviceUint(HDEVINFO devices, SP_DEVINFO_DATA& data, const DEVPROPKEY& key)
{
    DWORD type = 0;
    DWORD value = 0;
    if (!::SetupDiGetDevicePropertyW(devices, &data, &key, &type,
            reinterpret_cast<PBYTE>(&value), sizeof value, nullptr, 0)
        || type != DEVPROP_TYPE_UINT32)
        return {};
    wchar_t buffer[16];
    std::swprintf(buffer, 16, L"%u", value);
    return buffer;
}

// The bus a device sits on, from the instance path's first segment: "USB\...",
// "PCI\...", "ACPI\...", "ROOT\...".
inline std::wstring busName(const std::wstring& instanceId)
{
    const std::size_t slash = instanceId.find(L'\\');
    if (slash == std::wstring::npos)
        return {};
    const std::wstring prefix = instanceId.substr(0, slash);
    if (prefix == L"USB")
        return L"USB";
    if (prefix == L"PCI")
        return L"PCI";
    if (prefix == L"ACPI")
        return L"ACPI";
    if (prefix == L"ROOT")
        return L"Root";
    if (prefix == L"FTDIBUS")
        return L"FTDI";
    return prefix;
}

// The hardware id holds the parts that identify the chip: VID, PID and, for
// devices that have one, the serial number. "USB\VID_10C4&PID_EA60\0001" is
// split into what it says, without guessing at anything it does not. The list
// is a multi-string, and every entry is read: one of them carries the ids.
// FTDI parts use '+' as the separator ("VID_0403+PID_6001+AB123456A"), and
// "&MI_00" on a composite port marks the interface, so both signs split a
// token. VID_/PID_ are kept; nothing else is, so the serial is read separately.
inline void splitHardwareId(const std::wstring& ids, DeviceDetails& details)
{
    // The list is NUL-separated; each entry is one id.
    std::size_t start = 0;
    while (start < ids.size()) {
        std::size_t end = ids.find(L'\0', start);
        if (end == std::wstring::npos)
            end = ids.size();
        const std::wstring id = ids.substr(start, end - start);
        start = end + 1;

        // The id has one part per '\'; each part is "KEY_VALUE" or "KEY&VALUE".
        std::vector<std::wstring> parts;
        std::wstring current;
        for (wchar_t c : id) {
            if (c == L'\\') {
                parts.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        parts.push_back(current);

        for (const std::wstring& part : parts) {
            // The tokens of one part: "VID_10C4&PID_EA60" → "VID_10C4", "PID_EA60".
            std::wstring token;
            for (std::size_t i = 0; i <= part.size(); ++i) {
                const bool boundary = i == part.size() || part[i] == L'&' || part[i] == L'+';
                if (boundary) {
                    if (token.compare(0, 4, L"VID_") == 0 && details.vendorId.empty())
                        details.vendorId = token.substr(4);
                    else if (token.compare(0, 4, L"PID_") == 0 && details.productId.empty())
                        details.productId = token.substr(4);
                    token.clear();
                } else {
                    token.push_back(part[i]);
                }
            }
        }
    }
}

// The device's own serial, when its instance path carries one. A USB device
// shows up as "USB\VID_10C4&PID_EA60\0001", whose last part is the device's
// serial (the part Windows assigns); an FTDI port is
// "FTDIBUS\VID_0403+PID_6001+AB123456A\0000", whose serial is the third '+'
// segment of the middle. A generated last part (one that carries '&', like
// "7&1a2b3c4d&0&0000") is not the device's serial and is not shown.
inline void splitSerial(const std::wstring& instanceId, DeviceDetails& details)
{
    if (!details.serialNumber.empty())
        return;
    const std::size_t first = instanceId.find(L'\\');
    const std::size_t last = instanceId.rfind(L'\\');
    if (first == std::wstring::npos || last == std::wstring::npos || first == last)
        return;
    const std::wstring middle = instanceId.substr(first + 1, last - first - 1);
    if (middle.find(L'+') != std::wstring::npos) {
        const std::size_t firstPlus = middle.find(L'+');
        const std::size_t secondPlus = middle.find(L'+', firstPlus + 1);
        if (secondPlus != std::wstring::npos) {
            const std::wstring serial = middle.substr(secondPlus + 1);
            if (!serial.empty())
                details.serialNumber = serial;
        }
        return;
    }
    const std::wstring tail = instanceId.substr(last + 1);
    if (!tail.empty() && tail.find(L'&') == std::wstring::npos && tail != L"0000")
        details.serialNumber = tail;
}

// The device's hardware ids, as the registry lists them: a multi-string whose
// entries are the ids ("USB\VID_10C4&PID_EA60&REV_0100", "USB\VID_10C4&PID_EA60").
// A USB serial port's own ids are the chip's ids, so the port names its own
// chip; a composite device names it with the interface suffix ("&MI_00"), which
// the split ignores. As in deviceString, the sizing call's
// ERROR_INSUFFICIENT_BUFFER is the answer that carries the size.
inline std::wstring hardwareIds(HDEVINFO devices, SP_DEVINFO_DATA& data)
{
    DWORD kind = 0;
    DWORD size = 0;
    ::SetLastError(0);
    const BOOL sized = ::SetupDiGetDeviceRegistryPropertyW(devices, &data, SPDRP_HARDWAREID, &kind,
        nullptr, 0, &size);
    if (!sized && ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return {};
    if (size == 0)
        return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!::SetupDiGetDeviceRegistryPropertyW(devices, &data, SPDRP_HARDWAREID, &kind,
            reinterpret_cast<PBYTE>(value.data()), size, nullptr))
        return {};
    // A multi-string: the entries are NUL-separated, and all of them are
    // kept — one of them carries the chip's ids.
    return value;
}

// Everything Windows knows about one port device: the chip's ids from its own
// hardware ids, the serial from its instance path, and the labels Device Manager
// shows. A USB serial port is the USB device itself, so its own hardware ids
// name the chip ("USB\VID_10C4&PID_EA60&REV_0100"); its parent is the hub it is
// plugged into, and the hub's ids are the hub's, not the port's, so nothing is
// read from the parent. Each field is optional: a device Windows cannot describe
// leaves it empty, and nothing is invented to fill it.
inline void readDetails(HDEVINFO devices, SP_DEVINFO_DATA& data, const std::wstring& instanceId,
    DeviceDetails& details)
{
    details.busType = busName(instanceId);
    details.manufacturer = deviceString(devices, data, DEVPKEY_Device_Manufacturer);
    // What the chip calls itself, e.g. "CP2102 USB to UART Bridge Controller".
    details.chip = deviceString(devices, data, DEVPKEY_Device_BusReportedDeviceDesc);
    details.driverVersion = deviceString(devices, data, DEVPKEY_Device_DriverVersion);
    // Where it is plugged in: Windows' own "Port_#0002.Hub_#0006" when it has
    // one, else the first location path, which names the whole chain of buses.
    details.location = deviceString(devices, data, DEVPKEY_Device_LocationInfo);
    if (details.location.empty())
        details.location = deviceString(devices, data, DEVPKEY_Device_LocationPaths);

    // The chip the port is built from, and the serial it carries.
    splitHardwareId(hardwareIds(devices, data), details);
    splitSerial(instanceId, details);

    // Windows' own complaint about the device, when there is one.
    const std::wstring problem = deviceUint(devices, data, DEVPKEY_Device_ProblemCode);
    if (!problem.empty() && problem != L"0")
        details.problem = problem;
}

inline int comFromName(const wchar_t* name)
{
    if (!name)
        return 0;
    const std::wstring text = lowerWide(name);
    const std::size_t at = text.rfind(L"com");
    if (at == std::wstring::npos)
        return 0;
    int value = 0;
    std::size_t i = at + 3;
    bool digits = false;
    for (; i < text.size() && text[i] >= L'0' && text[i] <= L'9'; ++i) {
        digits = true;
        value = value * 10 + (text[i] - L'0');
        if (value > 4096)
            return 0;
    }
    return digits ? value : 0;
}

} // namespace detail

// Ports that the COM-port class enumerates, with the instance id that survives
// a replug under a different COM number (TIO-010).
inline std::vector<PortInfo> enumeratePorts()
{
    std::vector<PortInfo> ports;
    HDEVINFO devices = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof data;
        for (DWORD index = 0; ::SetupDiEnumDeviceInfo(devices, index, &data); ++index) {
            wchar_t name[256] = {};
            DWORD kind = 0;
            DWORD size = 0;
            // The friendly name ends with "(COMn)".
            if (!::SetupDiGetDeviceRegistryPropertyW(devices, &data, SPDRP_FRIENDLYNAME, &kind,
                    reinterpret_cast<PBYTE>(name), sizeof name, &size))
                continue;
            const int com = detail::comFromName(name);
            if (com <= 0)
                continue;

            PortInfo info;
            info.com = com;
            info.description = name;

            // The device instance id (HARDWARE\DEVICEMAP style is not stable
            // across a different USB socket, so the enum key is the better id).
            wchar_t instance[512] = {};
            if (::SetupDiGetDeviceInstanceIdW(devices, &data, instance, 512, nullptr))
                info.instanceId = instance;

            // What the device says about itself: the chip behind the port, the
            // driver that owns it, and where it is plugged in. Each field is
            // optional; what Windows does not have stays empty.
            detail::readDetails(devices, data, info.instanceId, info.details);

            ports.push_back(std::move(info));
        }
        ::SetupDiDestroyDeviceInfoList(devices);
    }

    // Merge SERIALCOMM so a legacy port outside the class is still listed. A
    // port already known keeps the richer identity.
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key)
        == ERROR_SUCCESS) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[512];
            DWORD nameSize = sizeof name;
            wchar_t value[256];
            DWORD valueSize = sizeof value;
            DWORD type = 0;
            const LSTATUS status = ::RegEnumValueW(key, index, name, &nameSize, nullptr, &type,
                reinterpret_cast<LPBYTE>(value), &valueSize);
            if (status != ERROR_SUCCESS)
                break;
            if (type != REG_SZ)
                continue;
            const int com = detail::comFromName(value);
            if (com <= 0)
                continue;
            const bool known = std::any_of(ports.begin(), ports.end(),
                [com](const PortInfo& p) { return p.com == com; });
            if (known)
                continue;
            PortInfo info;
            info.com = com;
            info.instanceId = name; // the device path, e.g. \Device\Silabser0
            info.description = value;
            ports.push_back(std::move(info));
        }
        ::RegCloseKey(key);
    }

    std::sort(ports.begin(), ports.end(),
        [](const PortInfo& a, const PortInfo& b) { return a.com < b.com; });
    return ports;
}

// Re-read one port's identity before a launch (TIO-010).
inline bool identityOf(int com, std::wstring& instanceId, std::wstring& description,
    DeviceDetails& details)
{
    for (const PortInfo& info : enumeratePorts()) {
        if (info.com != com)
            continue;
        instanceId = info.instanceId;
        description = info.description;
        details = info.details;
        return true;
    }
    return false;
}

// ---- SHA256 (TIO-002/003) -------------------------------------------------------

inline bool sha256File(const std::wstring& path, std::string& hex)
{
    hex.clear();
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest(32);
    bool ok = false;
    DWORD objectSize = 0;
    DWORD produced = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
        && BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
               reinterpret_cast<PUCHAR>(&objectSize), sizeof objectSize, &produced, 0) >= 0) {
        object.resize(objectSize);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0) {
            std::vector<unsigned char> buffer(64 * 1024);
            DWORD read = 0;
            ok = true;
            while (::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)
                   && read > 0) {
                if (BCryptHashData(hash, buffer.data(), read, 0) < 0) {
                    ok = false;
                    break;
                }
            }
            if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
                static const char* digits = "0123456789abcdef";
                for (const unsigned char byte : digest) {
                    hex.push_back(digits[byte >> 4]);
                    hex.push_back(digits[byte & 0x0F]);
                }
            } else {
                ok = false;
            }
        }
        if (hash)
            BCryptDestroyHash(hash);
    }
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    ::CloseHandle(file);
    if (!ok)
        hex.clear();
    return ok;
}

// ---- PE machine check -----------------------------------------------------------
//
// A DLL from another architecture loads as "not a valid Win32 application" at
// startup, which is a confusing failure to debug. Catching it before the launch
// names the component instead (TIO-002).

inline bool isPe64(const std::wstring& path, bool& is64, std::string& problem)
{
    is64 = false;
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        problem = "unreadable";
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    bool ok = ::ReadFile(file, &dos, sizeof dos, &read, nullptr) && read == sizeof dos
        && dos.e_magic == IMAGE_DOS_SIGNATURE;
    if (!ok) {
        problem = "not a PE file";
        ::CloseHandle(file);
        return false;
    }
    ::SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN);
    DWORD signature = 0;
    ok = ::ReadFile(file, &signature, sizeof signature, &read, nullptr) && read == sizeof signature
        && signature == IMAGE_NT_SIGNATURE;
    unsigned short machine = 0;
    if (ok) {
        IMAGE_FILE_HEADER header{};
        ok = ::ReadFile(file, &header, sizeof header, &read, nullptr) && read == sizeof header;
        machine = header.Machine;
    }
    ::CloseHandle(file);
    if (!ok) {
        problem = "truncated PE header";
        return false;
    }
    is64 = machine == IMAGE_FILE_MACHINE_AMD64;
    if (!is64)
        problem = machine == IMAGE_FILE_MACHINE_I386 ? "32-bit binary" : "unknown architecture";
    return is64;
}

// ---- package verification (TIO-002/003) -----------------------------------------

// One third-party component: what it is, where it came from, which licence
// text ships with it and why it is (or is not) inside the package. The machine
// readable twin of THIRD_PARTY.md, read from manifest/third-party.json.
struct ThirdParty {
    std::string name;
    std::string version;
    std::string license;
    bool bundled = false;
    std::string sourceLabel;
    std::string source;
    std::string extraLabel;
    std::string extra;
    std::vector<std::string> licenseFiles; // inside the package
    std::string whyZh;
    std::string whyEn;
};

struct VerifyResult {
    bool ok = false;
    int checked = 0;
    std::vector<std::string> problems; // UTF-8, one per failed file
    std::string packageVersion;        // from manifest/package.json, for the form
    // What the package carries, for the integrity card: the upstream it came
    // from, its licence, how many runtime files and how big they are.
    std::string sourceUrl;
    std::string sourceSha256;
    std::string license;
    int fileCount = 0;
    unsigned long long totalBytes = 0;
    std::vector<ThirdParty> thirdParty;
};

namespace detail {

// A manifest entry: relative path and its lowercase hex SHA256.
struct Entry {
    std::string path;
    std::string sha256;
    unsigned long long bytes = 0;
};

inline std::vector<Entry> readManifest(const std::wstring& pluginDir, std::string& version,
    std::string& problem)
{
    std::vector<Entry> entries;
    const std::wstring file = joinPath(joinPath(pluginDir, L"manifest"), L"files.json");
    if (!fileExists(file)) {
        problem = "manifest/files.json missing";
        return entries;
    }
    HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        problem = "manifest/files.json unreadable";
        return entries;
    }
    std::string text;
    char buffer[8192];
    DWORD read = 0;
    while (::ReadFile(handle, buffer, sizeof buffer, &read, nullptr) && read > 0)
        text.append(buffer, read);
    ::CloseHandle(handle);

    const auto parsed = winshun::Json::parse(text, &problem);
    if (!parsed || !parsed->isObject()) {
        problem = "manifest/files.json is not valid JSON";
        return entries;
    }
    version = (*parsed)["package"].toString();
    for (const winshun::Json& item : (*parsed)["files"].toArray()) {
        Entry entry;
        entry.path = item["path"].toString();
        entry.sha256 = item["sha256"].toString();
        entry.bytes = static_cast<unsigned long long>(item["bytes"].toInt(0));
        std::transform(entry.sha256.begin(), entry.sha256.end(), entry.sha256.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!entry.path.empty() && !entry.sha256.empty())
            entries.push_back(std::move(entry));
    }
    if (entries.empty())
        problem = "manifest/files.json lists no files";
    return entries;
}

// Reads a JSON file from the plugin folder into text. Empty when it is absent
// or unreadable: the caller decides whether that is a problem.
inline std::string readPluginFile(const std::wstring& pluginDir, const wchar_t* relative)
{
    const std::wstring file = joinPath(pluginDir, relative);
    if (!fileExists(file))
        return {};
    HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    std::string text;
    char buffer[8192];
    DWORD read = 0;
    while (::ReadFile(handle, buffer, sizeof buffer, &read, nullptr) && read > 0)
        text.append(buffer, read);
    ::CloseHandle(handle);
    return text;
}

// The third-party record, read from the machine readable twin of
// THIRD_PARTY.md. A missing or broken file yields no components: the window
// then shows the package's own files and says the record is unavailable, rather
// than inventing one.
inline std::vector<ThirdParty> readThirdParty(const std::wstring& pluginDir)
{
    std::vector<ThirdParty> components;
    std::string problem;
    const std::string text = readPluginFile(pluginDir, L"manifest\\third-party.json");
    if (text.empty())
        return components;
    const auto parsed = winshun::Json::parse(text, &problem);
    if (!parsed || !parsed->isObject())
        return components;
    for (const winshun::Json& item : (*parsed)["components"].toArray()) {
        ThirdParty component;
        component.name = item["name"].toString();
        component.version = item["version"].toString();
        component.license = item["license"].toString();
        component.bundled = item["bundled"].toBool(false);
        component.sourceLabel = item["sourceLabel"].toString();
        component.source = item["source"].toString();
        component.extraLabel = item["extraLabel"].toString();
        component.extra = item["extra"].toString();
        for (const winshun::Json& file : item["licenseFiles"].toArray())
            if (file.isString())
                component.licenseFiles.push_back(file.toString());
        component.whyZh = item["whyZh"].toString();
        component.whyEn = item["whyEn"].toString();
        if (!component.name.empty())
            components.push_back(std::move(component));
    }
    return components;
}

} // namespace detail

// Every packaged file must hash as recorded and be 64-bit. The manifest itself
// is not trusted authentication (it travels with the files), so the package is
// additionally pinned by the plugin's own SHA256 in release notes; here the job
// is to catch a broken or mixed install, not to defend against a rewritten one.
inline VerifyResult verifyPackage(const std::wstring& pluginDir)
{
    VerifyResult result;
    std::string problem;
    const std::vector<detail::Entry> entries = detail::readManifest(pluginDir, result.packageVersion, problem);
    if (entries.empty()) {
        result.problems.push_back(problem.empty() ? "manifest unreadable" : problem);
        return result;
    }
    // What the package carries, for the integrity card: the upstream it came
    // from, its licence, the runtime file count and their size.
    {
        std::string text;
        std::string readProblem;
        const std::string manifestText = detail::readPluginFile(pluginDir, L"manifest\\files.json");
        if (const auto parsed = winshun::Json::parse(manifestText, &readProblem)) {
            result.sourceUrl = (*parsed)["source"].toString();
            result.sourceSha256 = (*parsed)["sourceSha256"].toString();
            result.license = (*parsed)["license"].toString();
        }
    }
    result.fileCount = static_cast<int>(entries.size());
    for (const detail::Entry& entry : entries)
        result.totalBytes += entry.bytes;
    result.thirdParty = detail::readThirdParty(pluginDir);

    for (const detail::Entry& entry : entries) {
        const std::wstring path = joinPath(pluginDir, wide(entry.path));
        if (!fileExists(path)) {
            result.problems.push_back(entry.path + ": missing");
            continue;
        }
        // Only executables need the architecture check.
        if (entry.path.size() > 4) {
            const std::string tail = entry.path.substr(entry.path.size() - 4);
            if (tail == ".exe" || tail == ".dll") {
                bool is64 = false;
                std::string why;
                if (!isPe64(path, is64, why)) {
                    result.problems.push_back(entry.path + ": " + why);
                    continue;
                }
            }
        }
        std::string hash;
        if (!sha256File(path, hash)) {
            result.problems.push_back(entry.path + ": unreadable");
            continue;
        }
        if (hash != entry.sha256) {
            result.problems.push_back(entry.path + ": hash mismatch");
            continue;
        }
        ++result.checked;
    }
    result.ok = result.problems.empty();
    return result;
}

// ---- backend probing (TIO-005) -------------------------------------------------
//
// wt is preferred. Its version comes from the package registration, which is
// readable without starting the terminal; the execution alias in
// %LOCALAPPDATA%\Microsoft\WindowsApps is what SearchPath finds.

inline std::string windowsTerminalVersion()
{
    HKEY root = nullptr;
    const wchar_t* subkey =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\"
        L"AppModel\\Repository\\Packages";
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &root) != ERROR_SUCCESS)
        return {};
    std::string version;
    for (DWORD index = 0;; ++index) {
        wchar_t name[512];
        DWORD size = sizeof name / sizeof name[0];
        const LSTATUS status = ::RegEnumKeyExW(root, index, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (status != ERROR_SUCCESS)
            break;
        // Microsoft.WindowsTerminal_1.25.2733.0_x64__8wekyb3d8bbwe
        const std::wstring key(name, size);
        const std::wstring prefix = L"microsoft.windowsterminal_";
        if (lowerWide(key).rfind(prefix, 0) != 0)
            continue;
        const std::size_t begin = prefix.size();
        const std::size_t end = key.find(L'_', begin);
        if (end == std::wstring::npos)
            continue;
        const std::wstring found = key.substr(begin, end - begin);
        // Keep the highest version if a preview is installed next to stable.
        if (utf8(found) > version)
            version = utf8(found);
    }
    ::RegCloseKey(root);
    return version;
}

inline Backends probeBackends()
{
    Backends found;
    found.wt = !findOnPath(L"wt.exe").empty();
    found.conhost = fileExists(joinPath(systemDirectory(), L"conhost.exe"));
    if (found.wt)
        found.wtVersion = windowsTerminalVersion();
    return found;
}

// ---- process lookup -------------------------------------------------------------
//
// A PID alone is not an identity: Windows reuses numbers. The creation time is
// recorded at launch and compared before anything is terminated (TIO-007).

inline bool processCreationTime(DWORD pid, unsigned long long& created)
{
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    FILETIME start{}, exit{}, kernel{}, user{};
    bool ok = ::GetProcessTimes(process, &start, &exit, &kernel, &user) != FALSE;
    ::CloseHandle(process);
    if (!ok)
        return false;
    ULARGE_INTEGER value{};
    value.LowPart = start.dwLowDateTime;
    value.HighPart = start.dwHighDateTime;
    created = value.QuadPart;
    return true;
}

// Whether the process we launched is still the one this number belongs to, and
// still running. The creation time is the identity: after a restart the same
// PID belongs to someone else, and a session that outlived its process must not
// stay "running" in the UI (TIO-007).
inline bool processAlive(DWORD pid, unsigned long long created)
{
    if (pid == 0)
        return false;
    unsigned long long actual = 0;
    if (!processCreationTime(pid, actual))
        return false; // no such process (or no right to ask): it is gone
    return created == 0 || actual == created;
}

inline std::wstring processName(DWORD pid)
{
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring name;
    if (::QueryFullProcessImageNameW(process, 0, buffer, &size))
        name.assign(buffer, size);
    ::CloseHandle(process);
    return name;
}

inline std::wstring baseName(const std::wstring& path)
{
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

// ---- launch and stop ------------------------------------------------------------

struct LaunchResult {
    bool ok = false;
    std::string error; // UTF-8, already user-facing-neutral ("wt not found")
    DWORD pid = 0;
    unsigned long long created = 0;
};

namespace detail {

// A private PATH pointing at the package's runtime folder, so a stray MSYS2
// install elsewhere on the machine cannot lend tio a different msys-2.0.dll.
inline std::wstring privateEnvironment(const std::wstring& runtimeDir)
{
    const wchar_t* block = ::GetEnvironmentStringsW();
    std::vector<std::wstring> entries;
    if (block) {
        for (const wchar_t* at = block; *at; at += wcslen(at) + 1) {
            const std::wstring entry(at);
            if (lowerWide(entry).rfind(L"path=", 0) == 0)
                continue;
            entries.push_back(entry);
        }
        ::FreeEnvironmentStringsW(const_cast<wchar_t*>(block));
    }
    entries.push_back(L"PATH=" + runtimeDir);
    // The block must be sorted case-insensitively by name.
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        return lowerWide(a) < lowerWide(b);
    });
    std::wstring out;
    for (const std::wstring& entry : entries) {
        out += entry;
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

} // namespace detail

// Waits for the child that Windows Terminal created on our behalf. wt.exe only
// hands the command over and exits, so the tio process is a child of the
// resident WindowsTerminal.exe; the parent chain plus the launch time is what
// identifies it.
inline DWORD findWtChild(unsigned long long since, DWORD timeoutMs)
{
    const auto deadline = ::GetTickCount64() + timeoutMs;
    DWORD found = 0;
    int matches = 0;
    for (;;) {
        found = 0;
        matches = 0;
        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof entry;
            if (::Process32FirstW(snapshot, &entry)) {
                do {
                    const std::wstring name = baseName(entry.szExeFile);
                    if (lowerWide(name) != L"tio.exe")
                        continue;
                    const std::wstring parent = baseName(processName(entry.th32ParentProcessID));
                    if (lowerWide(parent) != L"windowsterminal.exe")
                        continue;
                    unsigned long long created = 0;
                    if (!processCreationTime(entry.th32ProcessID, created))
                        continue;
                    if (created < since)
                        continue;
                    found = entry.th32ProcessID;
                    ++matches;
                } while (::Process32NextW(snapshot, &entry));
            }
            ::CloseHandle(snapshot);
        }
        if (matches == 1)
            return found;
        if (::GetTickCount64() >= deadline)
            return 0; // none, or ambiguous: do not guess which one is ours
        ::Sleep(60);
    }
}

inline bool stopOwned(DWORD pid, unsigned long long created, DWORD gracefulMs = 2000)
{
    if (pid == 0)
        return true;
    unsigned long long actual = 0;
    if (!processCreationTime(pid, actual))
        return true; // already gone
    if (created != 0 && actual != created)
        return true; // the number now belongs to another process: leave it alone

    // tio stops on ctrl-c, so a launch we created ourselves gets a real chance
    // to leave cleanly. The console event only reaches a console we can attach
    // to; anything else (and the wt backend, whose tio runs in Windows
    // Terminal's console) falls back to terminating the one owned process.
    HANDLE process = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, pid);
    if (!process)
        return true;

    if (::AttachConsole(pid)) {
        ::SetConsoleCtrlHandler(nullptr, TRUE); // do not take our own process down
        ::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        ::FreeConsole();
        ::SetConsoleCtrlHandler(nullptr, FALSE);
        if (::WaitForSingleObject(process, gracefulMs) == WAIT_OBJECT_0) {
            ::CloseHandle(process);
            return true;
        }
    }

    const bool killed = ::TerminateProcess(process, 0xC000013AUL) != FALSE; // STATUS_CONTROL_C_EXIT
    ::WaitForSingleObject(process, 1500);
    ::CloseHandle(process);
    return killed;
}

struct LaunchContext {
    std::wstring pluginDir;
    std::wstring runtimeDir;
    std::wstring workDir; // dataDir: logs and the stop file live here
};

inline LaunchResult launch(const Plan& plan, const LaunchContext& context, DWORD wtTimeoutMs = 6000)
{
    LaunchResult result;
    if (plan.error != PlanError::None) {
        result.error = "plan invalid";
        return result;
    }
    const std::wstring commandLine = wide(buildCommandLine(plan.program));
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(L'\0');

    std::wstring environment = detail::privateEnvironment(context.runtimeDir);
    std::wstring cwd = context.runtimeDir;

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    // No STARTF_USESTDHANDLES: tio decides "interactive" from its own handles,
    // and a redirected stdout makes it print "Non-interactive mode enabled" and
    // never read the keyboard (measured: with no console it exits at the first
    // disconnect instead of waiting for input). The conhost path therefore asks
    // for a fresh console; the wt path must not, because Windows Terminal
    // creates the console for the tio it starts.
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT
        | (plan.backend == Backend::Conhost ? CREATE_NEW_CONSOLE : 0);
    PROCESS_INFORMATION info{};
    const unsigned long long before = [] {
        FILETIME now{};
        ::GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER value{};
        value.LowPart = now.dwLowDateTime;
        value.HighPart = now.dwHighDateTime;
        return value.QuadPart;
    }();

    const BOOL ok = ::CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, FALSE,
        flags, environment.data(), cwd.c_str(), &startup, &info);
    if (!ok) {
        const DWORD error = ::GetLastError();
        result.error = error == ERROR_FILE_NOT_FOUND ? "executable not found"
            : error == ERROR_ACCESS_DENIED ? "access denied (1816 means the host job forbids child processes)"
                                           : "CreateProcess failed (" + std::to_string(error) + ")";
        return result;
    }

    if (plan.backend == Backend::Conhost) {
        unsigned long long created = 0;
        processCreationTime(info.dwProcessId, created);
        result.pid = info.dwProcessId;
        result.created = created;
        result.ok = true;
        ::CloseHandle(info.hThread);
        ::CloseHandle(info.hProcess);
        return result;
    }

    // wt path: the stub exits quickly; the real tio belongs to Windows Terminal.
    ::WaitForSingleObject(info.hProcess, 4000);
    ::CloseHandle(info.hThread);
    ::CloseHandle(info.hProcess);

    const DWORD child = findWtChild(before, wtTimeoutMs);
    if (child == 0) {
        result.error = "wt started no tio process (or more than one appeared)";
        return result;
    }
    unsigned long long created = 0;
    if (!processCreationTime(child, created)) {
        result.error = "the tio process vanished before it could be recorded";
        return result;
    }
    result.pid = child;
    result.created = created;
    result.ok = true;
    return result;
}

} // namespace tioplug
