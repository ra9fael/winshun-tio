// controller.h - the state both entry points share.
//
// The plugin has two faces: the search-box rows WinShun draws from query() and
// action(), and the standalone QML window. Neither owns the configuration, so
// it lives here once: port, serial parameters, backend, session, package state.
// The protocol thread and the GUI thread both reach it, so every access takes
// the same mutex.
//
// No Qt and no windows in this header: only a callback saying "something
// changed" so the window can repaint.
#pragma once

#include "logic.h"
#include "native.h"
#include "snapshot.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <algorithm>
#include <vector>

namespace tioplug {

// What the user has entered. This is the unit that is saved, launched, and
// handed between the search box and the window.
struct Form {
    std::wstring port;
    Serial serial;
    Backend backend = Backend::None;

    Port parsed() const { return parsePort(winshun::utf8(port)); }
};

// Everything a view draws lives in snapshot.h; the controller fills one in.
class Controller {
public:
    using Notify = std::function<void()>;

    // Called on every change, from whichever thread changed the state. A view
    // marshals it to its own thread.
    void attach(Notify notify)
    {
        const std::lock_guard lock(m_mutex);
        m_notify = std::move(notify);
    }

    void configure(std::wstring pluginDir, std::string dataDir)
    {
        {
            const std::lock_guard lock(m_mutex);
            if (m_configured)
                return;
            m_pluginDir = absolutePath(std::move(pluginDir));
            m_dataDir = std::move(dataDir);
            m_runtimeDir = m_pluginDir + L"\\runtime";
            m_configured = true;
        }
        loadSettings();
        refresh();
    }

    // What the host's settings page holds (plugin.json's `settings`). It seeds a
    // value only when `tio.json` has never recorded one: the configuration the
    // user edited in the search box or the window is the more recent answer, so
    // a settings change must not silently reset a port back to a default.
    void setHostSettings(winshun::Json settings)
    {
        const std::lock_guard lock(m_mutex);
        m_hostSettings = std::move(settings);
    }

    // Re-probes backends, re-verifies the package, re-enumerates ports and
    // re-reads the device identity. Also notices a session whose process left
    // on its own (the user closed the terminal, or tio exited): without this
    // the window would keep saying "running" forever.
    void refresh()
    {
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            m_backends = probeBackends();
            m_package = verifyPackage(m_pluginDir);
            m_packageVersion = m_package.packageVersion;
            m_ports = enumeratePorts();
            reconcileSessionLocked();
            applySettingsLocked();
            refreshIdentityLocked();
            notify = m_notify;
        }
        if (notify)
            notify();
    }

private:
    // Marks a running session exited when its process is gone. The creation
    // time is the identity, so a reused PID is never mistaken for ours.
    void reconcileSessionLocked()
    {
        if (m_session.phase != Phase::Running || m_session.pid == 0)
            return;
        if (processAlive(m_session.pid, m_session.created))
            return;
        markExited(m_session);
        m_token.clear();
    }

public:

    Snapshot snapshot() const
    {
        const std::lock_guard lock(m_mutex);
        Snapshot out;
        out.port = m_form.parsed();
        out.serial = m_form.serial;
        out.backend = m_form.backend;
        out.backends = m_backends;
        out.session = m_session;
        out.package = m_package;
        out.ports = m_ports;
        out.identity = m_identity;
        out.identityTime = m_identityTime;
        out.identityName = m_identityName;
        out.identityDetails = m_identityDetails;
        out.identityValid = m_identityValid;
        out.packageVersion = m_packageVersion;
        out.token = m_token;
        out.readiness = readinessLocked();
        out.error = m_error;
        out.diagnostic = diagnosticsLocked();
        return out;
    }

    Form form() const
    {
        const std::lock_guard lock(m_mutex);
        return m_form;
    }

    // Persists the form and makes it the configuration. Returns false when the
    // file could not be written; the in-memory form is still adopted, so the
    // current session keeps working.
    bool save(const Form& form)
    {
        Notify notify;
        bool written = false;
        {
            const std::lock_guard lock(m_mutex);
            adoptLocked(form);
            written = writeSettingsLocked();
            notify = m_notify;
        }
        if (notify)
            notify();
        return written;
    }

    // Adopts the form without writing it. Used by the search box, where each
    // parameter change is a separate action but a settings write per keystroke
    // would be wasteful; the caller decides when to persist.
    void adopt(const Form& form)
    {
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            adoptLocked(form);
            writeSettingsLocked();
            notify = m_notify;
        }
        if (notify)
            notify();
    }

    // Starts tio. `error` is UTF-8 and already neutral ("wt started no tio
    // process"), never a localized sentence: callers localize.
    bool launch(const Form& form, std::string& error)
    {
        error.clear();
        Plan plan;
        LaunchContext context;
        Notify notify;

        // Phase 1: adopt, check, plan. All under the lock.
        {
            const std::lock_guard lock(m_mutex);
            adoptLocked(form);
            // The configuration the launch uses is the one that persists: what
            // started and what the next query shows must not differ.
            writeSettingsLocked();
            refreshIdentityLocked();

            if (!readinessLocked().ready()) {
                error = "a pre-launch check did not pass";
                m_error = L"";
                notify = m_notify;
            } else if (!beginValidation(m_session)) {
                error = "a session is already active";
                m_error = L"";
                notify = m_notify;
            } else {
                m_token = newToken();
                const Port port = m_form.parsed();
                m_session.com = port.com;
                m_session.device = port.device();
                m_session.token = m_token;

                const std::wstring wtPath = findOnPath(L"wt.exe");
                const std::wstring tioPath = m_runtimeDir + L"\\tio.exe";
                plan = planLaunch(m_form.backend,
                    wtPath.empty() ? std::string() : winshun::utf8(wtPath),
                    winshun::utf8(tioPath), port, m_form.serial, m_token);

                if (plan.error != PlanError::None) {
                    markFailed(m_session, PortState::Unknown);
                    error = plan.error == PlanError::PackagePath
                        ? "the plugin path holds a character Windows Terminal treats as a separator"
                        : "the launch arguments are invalid";
                    m_token.clear();
                } else if (plan.backend == Backend::None) {
                    markFailed(m_session, PortState::Unknown);
                    error = "no usable backend";
                    m_token.clear();
                } else {
                    context.pluginDir = m_pluginDir;
                    context.runtimeDir = m_runtimeDir;
                    context.workDir = winshun::wide(m_dataDir);
                    beginStart(m_session);
                }
                notify = m_notify;
            }
        }
        if (notify)
            notify();
        if (!error.empty())
            return false;

        // Phase 2: the launch itself waits on wt handing the process over, so it
        // runs without the lock; the views already show "starting".
        const LaunchResult result = tioplug::launch(plan, context);

        // Phase 3: record the outcome.
        bool started = false;
        {
            const std::lock_guard lock(m_mutex);
            if (!result.ok) {
                markFailed(m_session, PortState::Unknown);
                error = result.error;
                m_token.clear();
            } else {
                markRunning(m_session, result.pid, result.created);
                started = true;
            }
            // The last error, so the window can show it: a failed launch has to
            // say why somewhere the user is looking.
            m_error = started ? L"" : winshun::wide(error);
            notify = m_notify;
        }
        if (notify)
            notify();
        return started;
    }

    void stop(bool hostShutdown)
    {
        Notify notify;
        DWORD pid = 0;
        unsigned long long created = 0;
        bool active = false;
        {
            const std::lock_guard lock(m_mutex);
            active = m_session.active();
            if (active) {
                beginStop(m_session);
                pid = m_session.pid;
                created = m_session.created;
                notify = m_notify;
            }
        }
        if (notify)
            notify();
        if (!active)
            return;

        // Terminating can wait for the graceful timeout, so it runs unlocked.
        if (pid != 0)
            stopOwned(pid, created, hostShutdown ? 1200 : 2000);

        {
            const std::lock_guard lock(m_mutex);
            markExited(m_session);
            m_token.clear();
            notify = m_notify;
        }
        if (notify)
            notify();
    }

    std::string diagnostics() const
    {
        const std::lock_guard lock(m_mutex);
        return diagnosticsLocked();
    }

    bool configured() const
    {
        const std::lock_guard lock(m_mutex);
        return m_configured;
    }

    std::wstring error() const
    {
        const std::lock_guard lock(m_mutex);
        return m_error;
    }

    void setError(std::wstring text)
    {
        const std::lock_guard lock(m_mutex);
        m_error = std::move(text);
    }

    // Repaints the views without re-probing anything: for a change that only
    // affects how they draw (the host's language or theme).
    void touch()
    {
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            notify = m_notify;
        }
        if (notify)
            notify();
    }

    // Which cards the window keeps folded. The ids are the GroupCard titles, so
    // the list survives a restart and both faces could share it later.
    bool cardCollapsed(const std::string& id) const
    {
        const std::lock_guard lock(m_mutex);
        return std::find(m_collapsed.begin(), m_collapsed.end(), id) != m_collapsed.end();
    }

    bool setCardCollapsed(const std::string& id, bool collapsed)
    {
        winshun::Json::Object object;
        bool ok = false;
        {
            const std::lock_guard lock(m_mutex);
            const auto present = std::find(m_collapsed.begin(), m_collapsed.end(), id);
            if (collapsed && present == m_collapsed.end())
                m_collapsed.push_back(id);
            if (!collapsed && present != m_collapsed.end())
                m_collapsed.erase(present);
            winshun::Json::Array folded;
            for (const std::string& item : m_collapsed)
                folded.emplace_back(item);
            winshun::Json::Object ui;
            ui["collapsed"] = folded;
            object = m_persisted.isObject() ? m_persisted.toObject() : winshun::Json::Object{};
            object["ui"] = ui;
            ok = writeSettingsObjectLocked(object);
        }
        return ok;
    }

private:
    // wt is handed the tio path inside a command line that Windows Terminal
    // resolves against its own working directory, so a relative plugin dir would
    // start nothing. The host usually gives an absolute one; this makes it sure.
    static std::wstring absolutePath(std::wstring path)
    {
        if (path.empty())
            return path;
        wchar_t full[MAX_PATH * 2];
        const DWORD length = ::GetFullPathNameW(path.c_str(),
            static_cast<DWORD>(sizeof full / sizeof full[0]), full, nullptr);
        if (length == 0 || length >= sizeof full / sizeof full[0])
            return path;
        return std::wstring(full, length);
    }

    static std::string newToken()
    {
        // A session token, not a secret: it only has to be unique among the
        // tabs this launcher opens.
        static std::atomic<unsigned> counter{0};
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "s-%llx%x",
            static_cast<unsigned long long>(ms) & 0xFFFFFFFULL, counter.fetch_add(1) & 0xF);
        return buffer;
    }

    std::wstring settingsPathLocked() const
    {
        return winshun::wide(m_dataDir) + L"\\tio.json";
    }

    void loadSettings()
    {
        const std::lock_guard lock(m_mutex);
        m_persisted = winshun::Json::Object{};
        HANDLE file = ::CreateFileW(settingsPathLocked().c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        std::string text;
        char buffer[4096];
        DWORD read = 0;
        while (::ReadFile(file, buffer, sizeof buffer, &read, nullptr) && read > 0)
            text.append(buffer, read);
        ::CloseHandle(file);
        std::string problem;
        if (const auto parsed = winshun::Json::parse(text, &problem))
            if (parsed->isObject())
                m_persisted = *parsed;
        m_collapsed.clear();
        if (m_persisted.has("ui") && m_persisted["ui"].isObject()
            && m_persisted["ui"].has("collapsed") && m_persisted["ui"]["collapsed"].isArray()) {
            for (const auto& item : m_persisted["ui"]["collapsed"].toArray())
                if (item.isString())
                    m_collapsed.push_back(item.toString());
        }
    }

    void applySettingsLocked()
    {
        // A value comes from tio.json when it holds one, else from the host's
        // settings, else from the defaults in Serial. The order matters: what the
        // user edited in the search box or the window is more recent than the
        // settings page, so the page must not reset it.
        const auto saved = [this](const char* key) { return m_persisted.has(key); };

        // Port: the saved one, else the settings page's, else the first this
        // machine offers. The default is only a starting point; nothing launches
        // by itself.
        Port port = parsePort(saved("port") ? m_persisted["port"].toString()
                                            : m_hostSettings["port"].toString());
        if (!port.ok() && !m_ports.empty())
            port = parsePort("COM" + std::to_string(m_ports.front().com));
        if (port.ok())
            m_form.port = L"COM" + std::to_wstring(port.com);

        Serial serial;
        // A settings page may send a number or the text of a choice ("8"), so both
        // are read: `toInt` alone would silently take the fallback for a string.
        const auto integer = [](const winshun::Json& value, int fallback) {
            if (value.isNumber())
                return static_cast<int>(value.toInt(fallback));
            if (value.isString()) {
                const std::string text = value.toString();
                char* end = nullptr;
                const long parsed = std::strtol(text.c_str(), &end, 10);
                if (end != text.c_str() && *end == '\0')
                    return static_cast<int>(parsed);
            }
            return fallback;
        };
        const auto setting = [this, &saved](const char* key) -> const winshun::Json& {
            return saved(key) ? m_persisted[key] : m_hostSettings[key];
        };
        serial.baudrate = integer(setting("baudrate"), serial.baudrate);
        serial.dataBits = integer(setting("dataBits"), serial.dataBits);
        serial.stopBits = integer(setting("stopBits"), serial.stopBits);
        Parity parity = serial.parity;
        if (parseParity(setting("parity").toString(), parity))
            serial.parity = parity;
        Flow flow = serial.flow;
        if (parseFlow(setting("flow").toString(), flow))
            serial.flow = flow;
        m_form.serial = serial;

        // Backend: "auto" in the settings page is the preference order, and so is
        // a missing value. A saved choice wins while the machine still has it.
        Backend chosen = backendFromId(setting("backend").toString());
        m_form.backend = resolveBackend(m_backends, chosen);
    }

    void adoptLocked(const Form& form)
    {
        m_form = form;
        // A backend this machine does not have cannot be adopted: the form would
        // promise a terminal that will not start.
        if (form.backend != Backend::None || m_backends.wt || m_backends.conhost)
            m_form.backend = resolveBackend(m_backends, form.backend);
    }

    bool writeSettingsLocked()
    {
        winshun::Json::Object object;
        object["packageVersion"] = m_packageVersion;
        const Port port = m_form.parsed();
        object["port"] = port.ok() ? "COM" + std::to_string(port.com) : "";
        object["baudrate"] = static_cast<long long>(m_form.serial.baudrate);
        object["dataBits"] = static_cast<long long>(m_form.serial.dataBits);
        object["parity"] = std::string(parityName(m_form.serial.parity));
        object["stopBits"] = static_cast<long long>(m_form.serial.stopBits);
        object["flow"] = std::string(flowName(m_form.serial.flow));
        object["backend"] = std::string(backendId(m_form.backend));
        return writeSettingsObjectLocked(object);
    }

    // One writer for every persisted shape: the form fields and the window's
    // ui block both land in tio.json through here, so a fold never clobbers a
    // port and a save never loses a fold.
    bool writeSettingsObjectLocked(const winshun::Json::Object& object)
    {
        const std::string text = winshun::Json(object).dump();
        HANDLE file = ::CreateFileW(settingsPathLocked().c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            m_errorCode = "settings could not be written";
            return false;
        }
        DWORD written = 0;
        const bool ok = ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
                            nullptr)
            && written == text.size();
        ::CloseHandle(file);
        m_persisted = object;
        m_errorCode = ok ? std::string() : "settings were not written completely";
        return ok;
    }

    void refreshIdentityLocked()
    {
        m_identity.clear();
        m_identityTime.clear();
        m_identityName.clear();
        m_identityDetails = DeviceDetails();
        m_identityValid = false;
        const Port port = m_form.parsed();
        if (!port.ok())
            return;
        std::wstring id, description;
        DeviceDetails details;
        if (identityOf(port.com, id, description, details)) {
            m_identity = id;
            m_identityName = description;
            m_identityDetails = details;
            m_identityTime = nowText();
            m_identityValid = true;
        }
    }

    Readiness readinessLocked() const
    {
        Environment env;
        env.port = m_form.parsed();
        env.serial = m_form.serial;
        env.identityKnown = m_identityValid;
        env.packageValid = m_package.ok;
        env.backend = m_form.backend;
        env.sessionActive = m_session.active();
        return check(env);
    }

    static std::wstring nowText()
    {
        SYSTEMTIME now{};
        ::GetLocalTime(&now);
        wchar_t buffer[16];
        std::swprintf(buffer, 16, L"%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
        return buffer;
    }

    std::string diagnosticsLocked() const
    {
        std::string text = "ra9fael.tio launcher\n";
        text += "package   : "
            + (m_packageVersion.empty() ? std::string("not recorded") : m_packageVersion) + "\n";
        text += "integrity : " + std::string(m_package.ok ? "valid" : "failed");
        if (!m_package.ok) {
            text += " (";
            for (std::size_t i = 0; i < m_package.problems.size() && i < 3; ++i) {
                if (i)
                    text += "; ";
                text += m_package.problems[i];
            }
            text += ")";
        }
        text += "\nbackend   : " + std::string(backendId(m_form.backend));
        if (m_backends.wt)
            text += " (wt " + m_backends.wtVersion + ")";
        text += "\n";
        const Port port = m_form.parsed();
        text += "port      : "
            + (port.ok() ? "COM" + std::to_string(port.com) + " -> " + port.device()
                         : std::string("none"))
            + "\n";
        text += "session   : " + std::string(phaseId(m_session.phase));
        if (m_session.pid)
            text += " pid " + std::to_string(m_session.pid);
        text += " port " + std::string(portStateId(m_session.port)) + "\n";
        if (!m_errorCode.empty())
            text += "last error: " + m_errorCode + "\n";
        text += "privacy   : no serial payload is read or logged; the port path is kept out of shared logs\n";
        return text;
    }

    mutable std::mutex m_mutex;
    Notify m_notify;
    bool m_configured = false;
    std::wstring m_pluginDir;
    std::wstring m_runtimeDir;
    std::string m_dataDir;

    Form m_form;
    Session m_session;
    Backends m_backends;
    VerifyResult m_package;
    std::vector<PortInfo> m_ports;
    winshun::Json m_persisted;
    winshun::Json m_hostSettings;
    std::string m_packageVersion;
    std::string m_token;
    std::string m_errorCode;
    std::vector<std::string> m_collapsed; // folded card ids (TIO-008)
    std::wstring m_identity;
    std::wstring m_identityTime;
    std::wstring m_identityName;
    DeviceDetails m_identityDetails;
    bool m_identityValid = false;
    std::wstring m_error;
};

} // namespace tioplug
