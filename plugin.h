// plugin.h - the protocol face of ra9fael.tio.
//
// The same class serves both binaries: the console tio.exe (main.cpp) and
// TioApp.dll (qmlapp/qmlmain.cpp), which is started by the Qt stub. Keeping it
// here rather than in main.cpp is what lets the DLL run the identical protocol
// handler while the GUI thread draws the window.
#pragma once

#include "core.h"
#include "logic.h"
#include "native.h"
#include "rows.h"
#include "rows_build.h"
#include "window_hook.h"

#include <optional>
#include <string>
#include <vector>

namespace tioplug {

class TioPlugin final : public winshun::Plugin {
public:
    ~TioPlugin() override { core().controller().stopAll(true); }

protected:
    std::string name() const override { return "ra9fael.tio"; }

    void initialized() override
    {
        core().setLocale(locale());
        core().setTheme(theme());
        core().setAppearance(appearance());
        core().controller().setHostSettings(settings());
        core().configure(winshun::wide(pluginDir()), dataDir());
    }

    void settingsChanged(const winshun::Json& settings) override
    {
        // The host's settings page changed: adopt them only for what tio.json has
        // never recorded, then refresh so the rows show the result.
        core().controller().setHostSettings(settings);
        core().controller().refresh();
    }

    void appearanceChanged() override
    {
        core().setLocale(locale());
        core().setTheme(theme());
        core().setAppearance(appearance());
        // The window draws the host's language and look: tell it to repaint.
        // Nothing is re-probed, so a port is never touched for a colour change.
        core().controller().touch();
    }

    void shuttingDown() override
    {
        // The host ends this process about two seconds after the answer, so an
        // owned tio must be gone before then: an orphan would keep the serial
        // port after the launcher disappeared. Every session stops, not just
        // the focused one.
        core().controller().stopAll(true);
    }

    winshun::Items query(const winshun::Query& q) override
    {
        if (q.command != "tio")
            return {};

        // A query means the user typed again: the candidate list they were
        // browsing is no longer the answer, so the main list comes back.
        m_browse = Browse::None;
        return items(rows());
    }

    winshun::Effects action(const winshun::ActionRequest& request) override
    {
        if (request.command != "tio")
            return {notify(L"不支持的操作", "Unsupported action")};

        Controller& controller = core().controller();
        const std::string id = request.action;

        // ---- the diagnostics view's copy action ----
        // Shift+Enter on a diagnostics row (declared in the manifest): the whole
        // report copies, without leaving the view or the list.
        if (id == "copyDiagnostics") {
            return {winshun::effect::copy(controller.diagnostics()),
                notify(L"诊断已复制", "Diagnostics copied")};
        }

        // ---- launch and stop ----
        if (id == "launch") {
            std::string error;
            // The search box stays local: only the standalone window, where
            // the sharing switch and its warning are visible, opens the socket.
            const bool started = controller.launch(controller.form(), error, false);
            m_browse = Browse::None;
            if (!started) {
                // The launch row carries the same reason; the toast keeps it
                // visible when the list has scrolled the row out of sight.
                const std::string text = describe(error);
                return {winshun::effect::notify(text), replace(rows())};
            }
            return {replace(rows())};
        }
        if (id == "stop") {
            // The row's port is the one whose session stops: other sessions,
            // if the window opened any, keep running.
            controller.stopPort(controller.form().parsed().com);
            m_browse = Browse::None;
            return {replace(rows())};
        }
        if (id == "stopSession") {
            // One of the window's sessions, listed on its own row: the item id
            // carries the port ("stop:<com>"), and only that session stops.
            const std::string target = request.item["id"].toString();
            const std::size_t colon = target.find(':');
            if (colon != std::string::npos)
                controller.stopPort(std::atoi(target.c_str() + colon + 1));
            m_browse = Browse::None;
            return {replace(rows())};
        }

        // ---- one row per port: launch that port, stop that port ----
        // The port is in the item id ("port:<com>"), so the row decides which
        // port starts; the box's own configuration only supplies parameters.
        if (id == "launchPort" || id == "stopPort") {
            const std::string target = request.item["id"].toString();
            const std::size_t colon = target.find(':');
            const int com = colon == std::string::npos ? 0 : std::atoi(target.c_str() + colon + 1);
            m_browse = Browse::None;
            if (id == "stopPort") {
                if (com > 0)
                    controller.stopPort(com);
                return {replace(rows())};
            }
            if (com <= 0)
                return {notify(L"这一行没有端口", "This row names no port")};
            Form form = controller.form();
            form.port = L"COM" + winshun::wide(std::to_string(com));
            std::string error;
            if (!controller.launch(form, error, false))
                return {winshun::effect::notify(describe(error)), replace(rows())};
            return {replace(rows())};
        }

        // ---- the standalone window ----
        if (id == "openWindow") {
            if (!windowAvailable())
                return {notify(L"独立窗口需要 Qt 版插件", "The window needs the Qt build")};
            if (!openWindowHook()())
                return {notify(L"窗口打不开", "The window could not be opened")};
            // Keep the list: the user is about to come back to it.
            return {};
        }

        // ---- opening a candidate list ----
        if (id == "openPorts" || id == "openBaudrates" || id == "openBackends") {
            m_browse = id == "openPorts"       ? Browse::Ports
                : id == "openBaudrates"        ? Browse::Baudrates
                                               : Browse::Backends;
            return {replace(rows())};
        }
        if (id == "back") {
            m_browse = Browse::None;
            return {replace(rows())};
        }

        // ---- choosing from a candidate list ----
        if (id == "port" || id == "baud" || id == "backend") {
            const std::string target = request.item["id"].toString();
            const std::size_t colon = target.find(':');
            const std::string value = colon == std::string::npos ? std::string() : target.substr(colon + 1);
            Form form = controller.form();

            if (id == "port") {
                form.port = L"COM" + winshun::wide(value);
            } else if (id == "baud") {
                form.serial.baudrate = std::atoi(value.c_str());
                if (form.serial.baudrate <= 0)
                    return {notify(L"波特率无效", "Invalid baud rate")};
            } else {
                form.backend = backendFromId(value);
            }
            // Choosing is the whole edit: there is no Save in the search box, so
            // the configuration is written here.
            controller.adopt(form);
            m_browse = Browse::None;
            return {replace(rows())};
        }

        // ---- cycling a short list in place ----
        if (id == "cycleDataBits" || id == "cycleParity" || id == "cycleStopBits"
            || id == "cycleFlow") {
            Form form = controller.form();
            if (id == "cycleDataBits")
                cycleDataBits(form.serial);
            else if (id == "cycleParity")
                cycleParity(form.serial);
            else if (id == "cycleStopBits")
                cycleStopBits(form.serial);
            else
                cycleFlow(form.serial);
            controller.adopt(form);
            m_browse = Browse::None;
            return {replace(rows())};
        }

        // ---- diagnostics ----
        if (id == "diagnostics") {
            // Expand in the list rather than copy: the report is there to read.
            // Shift+Enter on the row copies the whole block (the row's `copy`),
            // which is the path for pasting it somewhere.
            m_browse = Browse::Diagnostics;
            return {replace(rows())};
        }

        return {notify(L"不支持的操作", "Unsupported action")};
    }

    std::optional<winshun::Json> details(const winshun::Json& item, const std::string& text) override
    {
        (void)item;
        (void)text;
        // The content pane shows the block the diagnostics action copies, so a
        // report can be read without copying it first.
        return winshun::Json::Object{{"text", core().controller().diagnostics()}};
    }

private:
    bool chinese() const { return locale() != "en"; }

    std::vector<Row> rows() const
    {
        return buildRows(core().controller().snapshot(), m_browse, makeLang(chinese()));
    }

    // Row -> the host's item type. Kept here so rows.h has no SDK dependency.
    static winshun::Items items(const std::vector<Row>& rows)
    {
        winshun::Items out;
        out.reserve(rows.size());
        for (const Row& row : rows) {
            winshun::Item item(row.id, row.title, row.subtitle);
            if (!row.icon.empty())
                item.withIcon(row.icon);
            item.withScore(row.score);
            if (row.disabled)
                item.invalid();
            if (row.checked)
                // A tick in the subtitle is how a candidate list shows the
                // current value without a new item field.
                item.subtitle = row.subtitle.empty() ? "✓" : row.subtitle + "  ✓";
            if (!row.copy.empty())
                item.withCopy(row.copy);
            if (!row.action.empty()) {
                std::vector<winshun::Action> actions;
                actions.push_back({row.action, item.title, row.actionKey});
                for (const auto& more : row.moreActions)
                    actions.push_back({more.first, more.first, more.second});
                item.withActions(std::move(actions));
            }
            out.push_back(std::move(item));
        }
        return out;
    }

    static winshun::Json replace(const std::vector<Row>& rows)
    {
        return winshun::effect::replace(items(rows));
    }

    winshun::Json notify(const wchar_t* zh, const char* en) const
    {
        return winshun::effect::notify(chinese() ? winshun::utf8(zh) : std::string(en));
    }

    // Turns a controller error code into something a user can act on. The
    // translation lives in core.h, so the window says the same thing.
    std::string describe(const std::string& code) const
    {
        return describeError(code, chinese());
    }

    Browse m_browse = Browse::None;
};

// One serve loop, shared by the console binary and the DLL's worker thread.
inline int runServeLoop()
{
    return TioPlugin().serve();
}

} // namespace tioplug
