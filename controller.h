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
    // re-reads the focused tab's identity. Also notices sessions whose process
    // left on its own (the user closed the terminal, or tio exited): without
    // this the window would keep saying "running" forever.
    void refresh()
    {
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            m_backends = probeBackends();
            m_package = verifyPackage(m_pluginDir);
            m_packageVersion = m_package.packageVersion;
            m_ports = enumeratePorts();
            reconcileSessionsLocked();
            applySettingsLocked();
            ensureTabLocked();
            refreshIdentityLocked();
            notify = m_notify;
        }
        if (notify)
            notify();
    }

private:
    // One session tab: a form bound to at most one process. Tabs may name the
    // same port while idle — the port becomes exclusive only when a session
    // starts on it, and the launch gates refuse a port another running session
    // owns. That is the difference between "a tab I am preparing" and "a port
    // one process holds".
    struct SessionTab {
        Form form;
        Session session;
    };

    // Marks running sessions exited when their processes are gone. The creation
    // time is the identity, so a reused PID is never mistaken for ours.
    void reconcileSessionsLocked()
    {
        for (SessionTab& tab : m_tabs) {
            if (tab.session.phase != Phase::Running || tab.session.pid == 0)
                continue;
            if (processAlive(tab.session.pid, tab.session.created))
                continue;
            markExited(tab.session);
        }
    }

    // The window always has something to draw: the first tab is seeded from the
    // default configuration, and it never goes away by itself.
    void ensureTabLocked()
    {
        if (m_tabs.empty()) {
            m_tabs.push_back({m_form, Session{}});
            m_activeTab = 0;
        }
        m_activeTab = std::min(m_activeTab, m_tabs.size() - 1);
    }

    SessionTab& activeTabLocked()
    {
        ensureTabLocked();
        return m_tabs[m_activeTab];
    }

    // The tab holding this port, if any. One port, one tab, ever.
    SessionTab* tabForPortLocked(int com)
    {
        for (SessionTab& tab : m_tabs)
            if (tab.form.parsed().com == com)
                return &tab;
        return nullptr;
    }

    // Whether another tab's *running* session owns this port. An idle tab naming
    // the same port is not a conflict: nothing holds the device until a launch.
    bool portRunningLocked(int com, const SessionTab* except) const
    {
        for (const SessionTab& tab : m_tabs)
            if (&tab != except && tab.form.parsed().com == com && tab.session.active())
                return true;
        return false;
    }

    // Const overloads for the read paths (snapshot, diagnostics): a snapshot
    // must be able to guarantee a tab exists without mutating through a const
    // this.
    void ensureTabLocked() const
    {
        const_cast<Controller*>(this)->ensureTabLocked();
    }

    const SessionTab& activeTabLocked() const
    {
        ensureTabLocked();
        return m_tabs[m_activeTab];
    }

    const SessionTab* tabForPortLocked(int com) const
    {
        for (const SessionTab& tab : m_tabs)
            if (tab.form.parsed().com == com)
                return &tab;
        return nullptr;
    }

public:

    Snapshot snapshot() const
    {
        const std::lock_guard lock(m_mutex);
        Snapshot out;
        out.port = m_form.parsed();
        out.serial = m_form.serial;
        out.backend = m_form.backend;
        out.share = m_form.share;
        out.sharePort = m_form.sharePort;
        out.backends = m_backends;
        out.package = m_package;
        out.ports = m_ports;
        out.identity = m_identity;
        out.identityTime = m_identityTime;
        out.identityName = m_identityName;
        out.identityDetails = m_identityDetails;
        out.identityValid = m_identityValid;
        out.packageVersion = m_packageVersion;
        ensureTabLocked();
        for (std::size_t i = 0; i < m_tabs.size(); ++i) {
            SessionView view;
            view.session = m_tabs[i].session;
            view.form = m_tabs[i].form;
            view.focused = i == m_activeTab;
            out.sessions.push_back(std::move(view));
        }
        // The focused tab's gates drive the window's Launch button; the default
        // form's gates drive the search box's launch row. The Session gate is
        // per target: one tab holding the port blocks launching THAT port, not
        // the others.
        out.readiness = readinessLocked(m_tabs[m_activeTab]);
        out.defaultReadiness = defaultReadinessLocked();
        out.error = m_error;
        out.diagnostic = diagnosticsLocked();
        return out;
    }

    Form form() const
    {
        const std::lock_guard lock(m_mutex);
        return m_form;
    }

    // The form the settings file last recorded. The window's dirty marker is
    // "differs from what is saved", not "differs from what is in memory": an
    // in-memory preview (a port picked there) is not a save, and the marker
    // must survive it.
    Form savedForm() const
    {
        const std::lock_guard lock(m_mutex);
        return m_savedForm;
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
    // would be wasteful; the caller decides when to persist. This edits the
    // default configuration only — tabs are bound when created or launched, and
    // a running session keeps the parameters it started with.
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

    // Adopts the form in memory into the focused tab — no settings write. The
    // window edits a copy and saves explicitly; picking a port there must
    // still re-read the identity (capture is automatic, never a button the
    // user must find), so the preview adopts and recaptures without
    // persisting. The saved form is untouched, so the window's dirty marker
    // keeps meaning "not saved yet". A port another running session owns is
    // adopted too: the launch gate names it, and refusing the pick would leave
    // the window unable to show which port the user just chose.
    bool preview(const Form& form, std::string& error)
    {
        error.clear();
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            SessionTab& tab = activeTabLocked();
            const Port before = tab.form.parsed();
            tab.form = form;
            resolveBackendLocked(tab.form);
            if (tab.form.parsed().com != before.com)
                refreshIdentityLocked();
            notify = m_notify;
        }
        if (notify)
            notify();
        return true;
    }

    // Starts tio for the port the form names, as its own session tab. A tab
    // already bound to the port is reused (its parameters updated while idle);
    // no other running session may own the port. A form without a port is
    // refused here and only here: a tab may be prepared before its port is
    // chosen, but nothing starts without one. `error` is UTF-8 and already
    // neutral ("wt started no tio process"), never a localized sentence:
    // callers localize. Sharing is on unless the form or caller turns it off.
    bool launch(const Form& form, std::string& error, bool allowRemoteShare = true)
    {
        error.clear();
        Plan plan;
        LaunchContext context;
        Notify notify;
        SessionTab* tab = nullptr;

        // Phase 1: bind a tab, check, plan. All under the lock.
        {
            const std::lock_guard lock(m_mutex);
            const Port target = form.parsed();
            tab = target.ok() ? tabForPortLocked(target.com) : nullptr;
            if (!target.ok()) {
                error = "no port is chosen";
                m_error = L"";
                notify = m_notify;
            } else if (portRunningLocked(target.com, tab)) {
                error = "a session is already active";
                m_error = L"";
                notify = m_notify;
            } else {
                if (!tab) {
                    m_tabs.push_back({form, Session{}});
                    tab = &m_tabs.back();
                }
                m_activeTab = static_cast<std::size_t>(tab - m_tabs.data());
                if (tab->session.phase == Phase::Configuring
                    || tab->session.phase == Phase::Exited
                    || tab->session.phase == Phase::Failed
                    || tab->session.phase == Phase::Removed)
                    tab->form = form; // idle: the tab takes the form's parameters
                resolveBackendLocked(tab->form);
                // The configuration the launch uses is the one that persists:
                // what started and what the next query shows must not differ.
                m_form = tab->form;
                // The search-box launch passes false and stays local; the
                // standalone window passes its own setting through.
                Form launchForm = tab->form;
                launchForm.share = tab->form.share && allowRemoteShare;
                writeSettingsLocked();
                refreshIdentityLocked();

                if (!readinessLocked(*tab).ready()) {
                    error = "a pre-launch check did not pass";
                    m_error = L"";
                } else if (launchForm.share
                    && !sharedSocketPortAvailable(launchForm.sharePort, error)) {
                    m_error = winshun::wide(error);
                    m_errorCode = error;
                } else if (!beginValidation(tab->session)) {
                    error = "a session is already active";
                    m_error = L"";
                } else {
                    const std::string token = newToken();
                    const Port port = tab->form.parsed();
                    tab->session.com = port.com;
                    tab->session.device = port.device();
                    tab->session.token = token;
                    tab->session.sharing = launchForm.share;
                    tab->session.sharePort = launchForm.sharePort;

                    const std::wstring wtPath = findOnPath(L"wt.exe");
                    const std::wstring tioPath = m_runtimeDir + L"\\tio.exe";
                    plan = planLaunch(launchForm.backend,
                        wtPath.empty() ? std::string() : winshun::utf8(wtPath),
                        winshun::utf8(tioPath), port, launchForm.serial, token,
                        launchForm.share, launchForm.sharePort);

                    if (plan.error != PlanError::None) {
                        markFailed(tab->session, PortState::Unknown);
                        if (plan.error == PlanError::PackagePath)
                            error = "the plugin path holds a character Windows Terminal treats as a separator";
                        else if (plan.error == PlanError::SharePort)
                            error = "the shared socket port must be between 1 and 65535";
                        else
                            error = "the launch arguments are invalid";
                    } else if (plan.backend == Backend::None) {
                        markFailed(tab->session, PortState::Unknown);
                        error = "no usable backend";
                    } else {
                        context.pluginDir = m_pluginDir;
                        context.runtimeDir = m_runtimeDir;
                        context.workDir = winshun::wide(m_dataDir);
                        beginStart(tab->session);
                    }
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
                markFailed(tab->session, PortState::Unknown);
                error = tab->session.sharing
                    ? "remote shared session failed to start: " + result.error : result.error;
            } else {
                markRunning(tab->session, result.pid, result.created);
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

    // Stops sessions. The window stops the focused tab; the search box stops
    // whichever session runs the port its row shows; host shutdown stops every
    // owned process, because an orphan would keep the serial port after the
    // launcher disappeared. `hostShutdown` only shortens the graceful timeout.
    void stopActive() { stopMatching([this](const SessionTab&, std::size_t index) {
        return index == m_activeTab; }); }
    void stopPort(int com) { stopMatching([com](const SessionTab& tab, std::size_t) {
        return tab.form.parsed().com == com; }); }
    void stopAll(bool hostShutdown) { stopMatching([](const SessionTab&, std::size_t) {
        return true; }, hostShutdown); }

private:
    template <typename Keep>
    void stopMatching(Keep keep, bool hostShutdown = false)
    {
        std::vector<std::pair<DWORD, unsigned long long>> victims;
        std::vector<std::size_t> indices;
        {
            const std::lock_guard lock(m_mutex);
            ensureTabLocked();
            for (std::size_t i = 0; i < m_tabs.size(); ++i) {
                SessionTab& tab = m_tabs[i];
                if (!keep(tab, i) || !tab.session.active() || !beginStop(tab.session))
                    continue;
                victims.emplace_back(tab.session.pid, tab.session.created);
                indices.push_back(i);
            }
        }
        if (victims.empty())
            return;
        // Terminating can wait for the graceful timeout, so it runs unlocked.
        for (const auto& [pid, created] : victims)
            if (pid != 0)
                stopOwned(pid, created, hostShutdown ? 1200 : 2000);
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            for (const std::size_t index : indices)
                if (index < m_tabs.size()) {
                    markExited(m_tabs[index].session);
                    m_tabs[index].session.token.clear();
                }
            notify = m_notify;
        }
        if (notify)
            notify();
    }

public:

    // A new tab, seeded with the given form. The seed may name no port at all:
    // a tab is a place to prepare a session, and it is the launch that needs a
    // port. Nothing is exclusive until a session starts, so two idle tabs may
    // name the same port.
    bool createSession(const Form& seed, std::string& error)
    {
        error.clear();
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            m_tabs.push_back({seed, Session{}});
            m_activeTab = m_tabs.size() - 1;
            refreshIdentityLocked();
                        m_error = L"";
            notify = m_notify;
        }
        if (notify)
            notify();
        return true;
    }

    // A new tab with the same parameters but no port: a clone is another session
    // to prepare, and the port is chosen in the card that shows it.
    bool cloneSession(std::size_t index, std::string& error)
    {
        Form seed;
        {
            const std::lock_guard lock(m_mutex);
            if (index >= m_tabs.size())
                return false;
            seed = m_tabs[index].form;
            seed.port.clear();
        }
        return createSession(seed, error);
    }

    // Closes a tab. A session that still owns a process refuses: the tab is
    // the only handle to it, and dropping it would orphan the terminal.
    bool closeSession(std::size_t index, std::string& error)
    {
        error.clear();
        Notify notify;
        bool closed = false;
        {
            const std::lock_guard lock(m_mutex);
            if (index >= m_tabs.size()) {
                error = "no session to stop";
            } else if (m_tabs[index].session.active()
                || m_tabs[index].session.phase == Phase::Validating) {
                error = "the session must be stopped before it is closed";
                m_error = winshun::wide(error);
            } else {
                m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
                if (m_activeTab >= m_tabs.size())
                    m_activeTab = m_tabs.empty() ? 0 : m_tabs.size() - 1;
                ensureTabLocked();
                refreshIdentityLocked();
                closed = true;
                m_error = L"";
            }
            notify = m_notify;
        }
        if (notify)
            notify();
        return closed;
    }

    // Switches the tab the window draws, and re-reads the identity for its
    // port: each tab names its own device, never another's.
    void focusSession(std::size_t index)
    {
        Notify notify;
        {
            const std::lock_guard lock(m_mutex);
            if (index < m_tabs.size() && index != m_activeTab) {
                m_activeTab = index;
                refreshIdentityLocked();
            }
            notify = m_notify;
        }
        if (notify)
            notify();
    }

public:

    std::string diagnostics() const
    {
        const std::lock_guard lock(m_mutex);
        return diagnosticsLocked();
    }

public:

    bool configured() const
    {
        const std::lock_guard lock(m_mutex);
        return m_configured;
    }

    // The plugin folder, so a view can open the package's own documents
    // (THIRD_PARTY.md, manifest/files.json) that ship beside the binaries.
    std::wstring pluginDir() const
    {
        const std::lock_guard lock(m_mutex);
        return m_pluginDir;
    }

    // A new hit-log path inside dataDir for the window's optional auto-log.
    // The folder is created here; the file itself appears on the first write.
    std::wstring newShareLogPath() const
    {
        const std::lock_guard lock(m_mutex);
        const std::wstring dir = shareLogDirLocked();
        if (dir.empty())
            return std::wstring();
        SYSTEMTIME now{};
        ::GetLocalTime(&now);
        wchar_t name[64];
        std::swprintf(name, 64, L"share-%04u%02u%02u-%02u%02u%02u.log",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        return dir + L"\\" + name;
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
    // Which cards the window keeps folded. The ids are the GroupCard titles, so
    // the list survives a restart and both faces could share it later. A card
    // the user has never toggled takes the `fallback` the view passes: the
    // details card starts folded, the rest start open. Once toggled, the
    // user's own choice wins for good — folding another card never resets it.
    bool cardCollapsed(const std::string& id, bool fallback = false) const
    {
        const std::lock_guard lock(m_mutex);
        if (std::find(m_touched.begin(), m_touched.end(), id) == m_touched.end())
            return fallback;
        return std::find(m_collapsed.begin(), m_collapsed.end(), id) != m_collapsed.end();
    }

    bool setCardCollapsed(const std::string& id, bool collapsed)
    {
        winshun::Json::Object object;
        bool ok = false;
        {
            const std::lock_guard lock(m_mutex);
            if (std::find(m_touched.begin(), m_touched.end(), id) == m_touched.end())
                m_touched.push_back(id);
            const auto present = std::find(m_collapsed.begin(), m_collapsed.end(), id);
            if (collapsed && present == m_collapsed.end())
                m_collapsed.push_back(id);
            if (!collapsed && present != m_collapsed.end())
                m_collapsed.erase(present);
            ok = writeUiLocked();
        }
        return ok;
    }

    // The window's "log the shared content" switch. It defaults to on: an
    // unattended serial session is exactly what a log is for, and a log that has
    // to be remembered is a log that is missing when it matters. Persisted with
    // the fold state, so turning it off stays off.
    bool shareLog() const
    {
        const std::lock_guard lock(m_mutex);
        return m_shareLog;
    }

    bool setShareLog(bool on)
    {
        const std::lock_guard lock(m_mutex);
        if (on == m_shareLog)
            return true;
        m_shareLog = on;
        return writeUiLocked();
    }

    // How the hex view draws the same bytes: bytes per row, and whether the
    // decode column is shown. Both are the reader's taste, remembered with the
    // fold state rather than reset on every launch.
    int shareHexRow() const
    {
        const std::lock_guard lock(m_mutex);
        return m_shareHexRow;
    }

    bool setShareHexRow(int bytes)
    {
        if (bytes != 8 && bytes != 16 && bytes != 32)
            return false;
        const std::lock_guard lock(m_mutex);
        if (bytes == m_shareHexRow)
            return true;
        m_shareHexRow = bytes;
        return writeUiLocked();
    }

    bool shareHexAscii() const
    {
        const std::lock_guard lock(m_mutex);
        return m_shareHexAscii;
    }

    bool setShareHexAscii(bool on)
    {
        const std::lock_guard lock(m_mutex);
        if (on == m_shareHexAscii)
            return true;
        m_shareHexAscii = on;
        return writeUiLocked();
    }

    // Where the window's auto-log writes. Created on demand: the folder only
    // exists because something is logged.
    std::wstring shareLogDir() const
    {
        const std::lock_guard lock(m_mutex);
        return shareLogDirLocked();
    }

private:
    // One writer for the window's ui block: folds, the log switch and the hex
    // view's shape. Every setter goes through it, so saving one never drops
    // another.
    bool writeUiLocked()
    {
        winshun::Json::Array folded;
        for (const std::string& item : m_collapsed)
            folded.emplace_back(item);
        winshun::Json::Array touched;
        for (const std::string& item : m_touched)
            touched.emplace_back(item);
        winshun::Json::Object ui;
        ui["collapsed"] = folded;
        ui["touched"] = touched;
        ui["shareLog"] = m_shareLog;
        ui["shareHexRow"] = static_cast<long long>(m_shareHexRow);
        ui["shareHexAscii"] = m_shareHexAscii;
        winshun::Json::Object object
            = m_persisted.isObject() ? m_persisted.toObject() : winshun::Json::Object{};
        object["ui"] = ui;
        return writeSettingsObjectLocked(object);
    }

    std::wstring shareLogDirLocked() const
    {
        if (m_dataDir.empty())
            return std::wstring();
        const std::wstring dir = winshun::wide(m_dataDir) + L"\\sessions";
        ::CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

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
        m_touched.clear();
        if (m_persisted.has("ui") && m_persisted["ui"].isObject()) {
            const winshun::Json& ui = m_persisted["ui"];
            if (ui.has("collapsed") && ui["collapsed"].isArray()) {
                for (const auto& item : ui["collapsed"].toArray())
                    if (item.isString())
                        m_collapsed.push_back(item.toString());
            }
            if (ui.has("touched") && ui["touched"].isArray()) {
                for (const auto& item : ui["touched"].toArray())
                    if (item.isString())
                        m_touched.push_back(item.toString());
            }
            m_shareLog = ui.has("shareLog") ? ui["shareLog"].toBool(true) : true;
            m_shareHexRow = ui.has("shareHexRow")
                ? static_cast<int>(ui["shareHexRow"].toInt(16)) : 16;
            if (m_shareHexRow != 8 && m_shareHexRow != 16 && m_shareHexRow != 32)
                m_shareHexRow = 16;
            m_shareHexAscii = ui.has("shareHexAscii")
                ? ui["shareHexAscii"].toBool(false) : false;
        } else {
            m_shareLog = true;
            m_shareHexRow = 16;
            m_shareHexAscii = false;
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

        // Sharing is on by default (the standalone window is its local
        // consumer); the user may turn it off or change the port in the
        // standalone window. The search box still launches without it.
        m_form.share = setting("share").toBool(true);
        m_form.sharePort = integer(setting("sharePort"), 54321);
        if (m_form.sharePort < 1024 || m_form.sharePort > 65535)
            m_form.sharePort = 54321;
        m_savedForm = m_form;
    }

    void adoptLocked(const Form& form)
    {
        m_form = form;
        resolveBackendLocked(m_form);
    }

    // A backend this machine does not have cannot stay in a form: it would
    // promise a terminal that will not start.
    void resolveBackendLocked(Form& form) const
    {
        if (form.backend != Backend::None || m_backends.wt || m_backends.conhost)
            form.backend = resolveBackend(m_backends, form.backend);
    }

    bool writeSettingsLocked()
    {
        // Start from what is on disk: the file also carries the window's ui
        // block, and a save must not drop a fold the user chose.
        winshun::Json::Object object
            = m_persisted.isObject() ? m_persisted.toObject() : winshun::Json::Object{};
        object["packageVersion"] = m_packageVersion;
        const Port port = m_form.parsed();
        object["port"] = port.ok() ? "COM" + std::to_string(port.com) : "";
        object["baudrate"] = static_cast<long long>(m_form.serial.baudrate);
        object["dataBits"] = static_cast<long long>(m_form.serial.dataBits);
        object["parity"] = std::string(parityName(m_form.serial.parity));
        object["stopBits"] = static_cast<long long>(m_form.serial.stopBits);
        object["flow"] = std::string(flowName(m_form.serial.flow));
        object["backend"] = std::string(backendId(m_form.backend));
        object.erase("capture");
        object["share"] = m_form.share;
        object["sharePort"] = static_cast<long long>(m_form.sharePort);
        const bool written = writeSettingsObjectLocked(object);
        // The saved form is what "dirty" is measured against, whether or not
        // the file took the write: a failed write must not look saved.
        if (written) {
            m_savedForm = m_form;
        }
        return written;
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
        ensureTabLocked();
        const Port port = m_tabs[m_activeTab].form.parsed();
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

    // The focused tab's gates: these drive the window's Launch button. The
    // Session gate is the tab's own — a tab with a running session cannot start
    // a second one on the same port; other tabs are unaffected.
    Readiness readinessLocked(const SessionTab& tab) const
    {
        Environment env;
        env.port = tab.form.parsed();
        env.portBusyByOther = env.port.ok() && portRunningLocked(env.port.com, &tab);
        env.shareEnabled = tab.form.share;
        env.sharePort = tab.form.sharePort;
        env.serial = tab.form.serial;
        env.identityKnown = m_identityValid;
        env.packageValid = m_package.ok;
        env.backend = tab.form.backend;
        env.sessionActive = tab.session.active();
        return check(env);
    }

    // The default form's gates: these drive the search box's launch row. Its
    // PortBusy gate is on while any session runs on the default port, and its
    // Session gate is on when one of them is that form's own.
    Readiness defaultReadinessLocked() const
    {
        Environment env;
        env.port = m_form.parsed();
        env.shareEnabled = false;
        env.sharePort = m_form.sharePort;
        env.serial = m_form.serial;
        const SessionTab* bound = tabForPortLocked(env.port.com);
        env.identityKnown = m_identityValid;
        env.packageValid = m_package.ok;
        env.backend = m_form.backend;
        env.sessionActive = bound && bound->session.active();
        env.portBusyByOther = env.port.ok() && !env.sessionActive
            && portRunningLocked(env.port.com, nullptr);
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
        text += "\nsessions  : ";
        if (m_tabs.empty()) {
            text += "none";
        } else {
            for (std::size_t i = 0; i < m_tabs.size(); ++i) {
                const SessionTab& tab = m_tabs[i];
                if (i)
                    text += " · ";
                const Port tabPort = tab.form.parsed();
                text += tabPort.ok() ? "COM" + std::to_string(tabPort.com) : std::string("no port");
                text += " " + std::string(phaseId(tab.session.phase));
                if (tab.form.share)
                    text += " share:" + std::to_string(tab.form.sharePort);
                if (tab.session.pid)
                    text += " pid " + std::to_string(tab.session.pid);
                if (i == m_activeTab)
                    text += " (focused)";
            }
        }
        text += "\n";
        if (!m_errorCode.empty())
            text += "last error: " + m_errorCode + "\n";
        const SessionTab& focused = activeTabLocked();
        text += std::string("remote    : ") + (focused.session.sharing
            ? "active on all interfaces, port " + std::to_string(focused.session.sharePort)
            : "off") + "\n";
        text += "warning   : remote socket clients can write bytes to the serial device\n";
        return text;
    }

    mutable std::mutex m_mutex;
    Notify m_notify;
    bool m_configured = false;
    std::wstring m_pluginDir;
    std::wstring m_runtimeDir;
    std::string m_dataDir;

    Form m_form;
    std::vector<SessionTab> m_tabs; // one port per tab; the window's tabs
    std::size_t m_activeTab = 0;    // the tab the window draws
    Backends m_backends;
    VerifyResult m_package;
    std::vector<PortInfo> m_ports;
    winshun::Json m_persisted;
    winshun::Json m_hostSettings;
    std::string m_packageVersion;
    std::string m_errorCode;
    std::vector<std::string> m_collapsed; // folded card ids (TIO-008)
    std::vector<std::string> m_touched;   // cards the user has folded/unfolded once
    // The window's "log the received bytes" switch: on unless turned off, and
    // remembered across restarts. It belongs to the window, not to a session.
    bool m_shareLog = true;
    // The hex view's shape, remembered with the folds: bytes per row (8/16/32)
    // and whether the decode column is shown.
    int m_shareHexRow = 16;
    bool m_shareHexAscii = false;
    std::wstring m_identity;
    Form m_savedForm;
    std::wstring m_identityTime;
    std::wstring m_identityName;
    DeviceDetails m_identityDetails;
    bool m_identityValid = false;
    std::wstring m_error;
};

} // namespace tioplug
