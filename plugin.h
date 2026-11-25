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

#include <cctype>
#include <cstdlib>
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
        // A change made in the search box or the window goes back to WinShun's
        // settings page, so its card and this plugin show one value. The page
        // persists it and answers with settingsChanged, which is applied here
        // without another write.
        core().controller().attachSettingsWriter([this](winshun::Json values) {
            updateSettings(std::move(values));
        });
        core().configure(winshun::wide(pluginDir()), dataDir());
    }

    void settingsChanged(const winshun::Json& settings) override
    {
        // The page changed: its three keys are the user's latest answer, so they
        // are adopted and the rows follow. Nothing is sent back from here.
        core().controller().setHostSettings(settings);
        core().controller().refresh();
    }

    // The page's "设置" button (manifest `settingsWindow`). The window is the
    // detailed configuration; opening it never touches a serial port.
    void showSettings() override
    {
        if (!windowAvailable()) {
            notifyUser(chinese() ? "设置窗口需要 Qt 版插件" : "The settings window needs the Qt build");
            return;
        }
        if (!openWindowHook()())
            notifyUser(chinese() ? "窗口打不开" : "The window could not be opened");
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
        if (q.command == "tio-io")
            return ioQuery(q.text);
        if (q.command != "tio")
            return {};

        // The ports are read here, not only when the process started: this plugin
        // is resident, so a device plugged in or pulled out since then is what the
        // list has to show. The rerun below keeps it true while the list is on
        // screen, without the user typing again.
        core().controller().refreshPorts();
        // A candidate list stays until the words change. The host asks the same
        // query again for a rerun, and that is not the user typing: dropping the
        // list then would snatch the baud rates away while they are being read.
        if (q.source != "command" && q.raw != m_answeredRaw)
            m_browse = Browse::None;
        if (q.source != "command")
            m_answeredRaw = q.raw;
        return items(rows(q.source == "command" ? Browse::None : m_browse));
    }

    // The rows are live: while they are shown the host asks again every few
    // seconds, so a serial device coming or going shows up by itself. The ids
    // stay the same, so the keyboard stays on its row.
    winshun::Json queryResult(const winshun::Query& q) override
    {
        winshun::Json::Object answer {{"items", winshun::itemsJson(query(q))}};
        if (q.command == "tio")
            answer["rerun"] = static_cast<long long>(3);
        return winshun::Json(std::move(answer));
    }

    winshun::Effects action(const winshun::ActionRequest& request) override
    {
        if (request.command == "tio-io")
            return ioAction(request);
        if (request.command != "tio")
            return {notify(L"不支持的操作", "Unsupported action")};

        Controller& controller = core().controller();
        const std::string id = request.action;

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
        if (id == "openBaudrates") {
            m_browse = Browse::Baudrates;
            return {replace(rows())};
        }
        if (id == "back") {
            m_browse = Browse::None;
            return {replace(rows())};
        }

        // ---- choosing from a candidate list ----
        if (id == "baud") {
            const std::string target = request.item["id"].toString();
            const std::size_t colon = target.find(':');
            const std::string value = colon == std::string::npos ? std::string() : target.substr(colon + 1);
            Form form = controller.form();

            form.serial.baudrate = std::atoi(value.c_str());
            if (form.serial.baudrate <= 0)
                return {notify(L"波特率无效", "Invalid baud rate")};
            // Choosing is the whole edit: there is no Save in the search box,
            // so the key the user changed is written here, and only that key.
            controller.adopt(form, {ConfigKey::Baudrate});
            m_browse = Browse::None;
            return {replace(rows())};
        }

        return {notify(L"不支持的操作", "Unsupported action")};
    }

    std::optional<winshun::Json> details(const winshun::Json& item, const std::string& text) override
    {
        (void)item;
        (void)text;
        // The host's details pane: the same report the window's diagnostics card
        // shows, so it can be read without opening the window.
        return winshun::Json::Object{{"text", core().controller().diagnostics()}};
    }

private:
    static std::vector<std::string> tokens(const std::string& text)
    {
        std::vector<std::string> out;
        std::string current;
        bool quoted = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (quoted && c == '\\' && i + 1 < text.size()) {
                current.push_back(c);
                current.push_back(text[++i]);
                continue;
            }
            if (c == '"') {
                quoted = !quoted;
            } else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
                if (!current.empty()) {
                    out.push_back(std::move(current));
                    current.clear();
                }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty())
            out.push_back(std::move(current));
        return out;
    }

    static bool hexDigit(char c, unsigned& value)
    {
        if (c >= '0' && c <= '9') value = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') value = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        return true;
    }

    static bool decodeHex(const std::string& input, std::string& out)
    {
        std::string digits;
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (std::isspace(static_cast<unsigned char>(input[i])))
                continue;
            if (input[i] == '0' && i + 1 < input.size() && (input[i + 1] == 'x' || input[i + 1] == 'X')) {
                ++i;
                continue;
            }
            digits.push_back(input[i]);
        }
        if (digits.empty() || digits.size() % 2 != 0 || digits.size() / 2 > 4096)
            return false;
        out.clear();
        out.reserve(digits.size() / 2);
        for (std::size_t i = 0; i < digits.size(); i += 2) {
            unsigned high = 0, low = 0;
            if (!hexDigit(digits[i], high) || !hexDigit(digits[i + 1], low))
                return false;
            out.push_back(static_cast<char>((high << 4) | low));
        }
        return true;
    }

    static bool decodeAscii(const std::string& input, std::string& out)
    {
        out.clear();
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (input[i] != '\\') {
                out.push_back(input[i]);
                continue;
            }
            if (++i >= input.size())
                return false;
            const char c = input[i];
            if (c == 'r') out.push_back('\r');
            else if (c == 'n') out.push_back('\n');
            else if (c == 't') out.push_back('\t');
            else if (c == '0') out.push_back('\0');
            else if (c == '\\' || c == '"') out.push_back(c);
            else if (c == 'x' && i + 2 < input.size()) {
                unsigned high = 0, low = 0;
                if (!hexDigit(input[i + 1], high) || !hexDigit(input[i + 2], low))
                    return false;
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
            } else {
                return false;
            }
        }
        return !out.empty() && out.size() <= 4096;
    }

    static bool number(const std::string& text, std::uint64_t& value)
    {
        if (text.empty())
            return false;
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
        if (!end || *end != '\0')
            return false;
        value = parsed;
        return true;
    }

    static winshun::Item errorItem(const std::string& text)
    {
        winshun::Item item("error", text);
        item.invalid();
        return item;
    }

    winshun::Items ioQuery(const std::string& text) const
    {
        const std::vector<std::string> args = tokens(text);
        if (args.empty())
            return {errorItem("Use status, read COMn, or send COMn")};
        if (args[0] == "status")
            return ioStatus();
        if (args[0] != "read" && args[0] != "send")
            return {errorItem("Use status, read COMn, or send COMn")};
        if (args.size() < 2)
            return {errorItem("A COM port is required")};
        const Port port = parsePort(args[1]);
        if (!port.ok())
            return {errorItem("Invalid COM port")};

        std::string format = "ascii";
        std::string data;
        std::uint64_t from = 0;
        std::uint64_t max = 2048;
        bool haveFrom = false;
        bool haveData = false;
        bool haveTail = false;
        std::string requestedToken;
        bool dataOption = false;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (dataOption) {
                if (args[i].rfind("--", 0) == 0) {
                    dataOption = false;
                    --i;
                    continue;
                }
                if (!data.empty()) data.push_back(' ');
                data += args[i];
                continue;
            }
            if (args[i] == "--format" && i + 1 < args.size()) format = args[++i];
            else if (args[i] == "--data" && i + 1 < args.size()) { data = args[++i]; haveData = true; dataOption = true; }
            else if (args[i] == "--from" && i + 1 < args.size()) { haveFrom = number(args[++i], from); if (!haveFrom) return {errorItem("Invalid cursor")}; }
            else if (args[i] == "--tail" && i + 1 < args.size()) { if (!number(args[++i], max)) return {errorItem("Invalid tail")}; haveTail = true; }
            else if (args[i] == "--max" && i + 1 < args.size()) { if (!number(args[++i], max)) return {errorItem("Invalid max")}; }
            else if (args[i] == "--session" && i + 1 < args.size()) { requestedToken = args[++i]; }
            else return {errorItem("Unknown option")};
        }
        std::string token;
        std::uint64_t generation = 0;
        const std::shared_ptr<ShareClient> tap = core().controller().sharedTap(port.com, token, generation);
        if (!tap)
            return {errorItem("COM" + std::to_string(port.com) + " is not a user-shared session")};
        if (!requestedToken.empty() && requestedToken != token)
            return {errorItem("The requested session is no longer active")};
        if (args[0] == "send") {
            if (!tap->connected())
                return {errorItem("The shared socket is not connected")};
            if (!haveData)
                return {errorItem("--data is required")};
            std::string bytes;
            const bool decoded = format == "hex" ? decodeHex(data, bytes) : format == "ascii" ? decodeAscii(data, bytes) : false;
            if (!decoded)
                return {errorItem("Invalid data or format")};
            winshun::Item item("send:" + std::to_string(port.com), "Send " + std::to_string(bytes.size()) + " bytes to COM" + std::to_string(port.com));
            item.subtitle = format + " generation=" + std::to_string(generation);
            item.withActions({{"sendSerial", "Send", "Enter"}});
            item.withData(winshun::Json::Object {{"com", port.com}, {"token", token}, {"generation", static_cast<unsigned long long>(generation)}, {"bytesHex", hexDump(bytes)}});
            return {std::move(item)};
        }
        if (format != "hex" && format != "ascii")
            return {errorItem("Format must be hex or ascii")};
        if (max == 0 || max > 4096)
            return {errorItem("max must be between 1 and 4096")};
        std::uint64_t cursor = haveFrom ? from : tap->oldestBytes();
        if (haveTail) {
            const std::uint64_t end = tap->totalBytes();
            cursor = end > max ? end - max : tap->oldestBytes();
        }
        bool overrun = false;
        const std::uint64_t requested = cursor;
        const std::string payload = tap->read(cursor, static_cast<std::size_t>(max), overrun);
        winshun::Item item("read:" + std::to_string(port.com), format == "hex" ? "Read bytes from COM" + std::to_string(port.com) : "Read text from COM" + std::to_string(port.com));
        item.invalid();
        item.subtitle = format + " generation=" + std::to_string(generation) + " cursor=" + std::to_string(cursor)
            + (overrun ? " data lost before cursor " + std::to_string(requested) : "");
        item.withCopy(format == "hex" ? hexDump(payload) : asciiDump(payload));
        item.withData(winshun::Json::Object {{"com", port.com}, {"token", token}, {"generation", static_cast<unsigned long long>(generation)}, {"cursor", static_cast<unsigned long long>(cursor)}});
        return {std::move(item)};
    }

    winshun::Items ioStatus() const
    {
        winshun::Items out;
        const Snapshot snapshot = core().controller().snapshot();
        for (const SessionView& view : snapshot.sessions) {
            if (!view.session.active() || !view.session.sharing)
                continue;
            std::string token;
            std::uint64_t generation = 0;
            const auto tap = core().controller().sharedTap(view.session.com, token, generation);
            winshun::Item item("status:" + std::to_string(view.session.com), "COM" + std::to_string(view.session.com));
            item.invalid();
            item.subtitle = std::string(tap && tap->connected() ? "connected" : "connecting") + " port=" + std::to_string(view.session.sharePort);
            item.withCopy(winshun::Json(winshun::Json::Object {{"com", view.session.com}, {"shared", true},
                {"connected", tap && tap->connected()}, {"session", token},
                {"generation", static_cast<unsigned long long>(generation)},
                {"cursor", static_cast<unsigned long long>(tap ? tap->totalBytes() : 0)}}).dump());
            out.push_back(std::move(item));
        }
        if (out.empty())
            out.push_back(errorItem("No user-shared session is running"));
        return out;
    }

    winshun::Effects ioAction(const winshun::ActionRequest& request)
    {
        if (request.action != "sendSerial")
            return {winshun::effect::notify("Unsupported serial I/O action")};
        const winshun::Json& data = request.item["data"];
        if (!data.isObject() || !data.has("com") || !data.has("token") || !data.has("generation") || !data.has("bytesHex"))
            return {winshun::effect::notify("The send request is incomplete")};
        const int com = static_cast<int>(data["com"].toInt());
        const std::string expectedToken = data["token"].toString();
        const std::uint64_t expectedGeneration = static_cast<std::uint64_t>(data["generation"].toNumber());
        std::string bytes;
        if (!decodeHex(data["bytesHex"].toString(), bytes))
            return {winshun::effect::notify("The send bytes are invalid")};
        return core().controller().writeShared(com, expectedToken, expectedGeneration, bytes)
            ? winshun::Effects {}
            : winshun::Effects {winshun::effect::notify("The shared session changed or did not accept the bytes")};
    }

    static std::string hexDump(const std::string& bytes)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (unsigned char c : bytes) {
            if (!out.empty()) out.push_back(' ');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 15]);
        }
        return out;
    }

    static std::string asciiDump(const std::string& bytes)
    {
        std::string out;
        for (unsigned char c : bytes) {
            switch (c) {
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n\n"; break;
            case '\t': out += "\\t"; break;
            case '\0': out += "\\0"; break;
            default:
                if (c >= 0x20 && c <= 0x7e)
                    out.push_back(static_cast<char>(c));
                else {
                    static const char* digits = "0123456789abcdef";
                    out += "\\x";
                    out.push_back(digits[c >> 4]);
                    out.push_back(digits[c & 15]);
                }
            }
        }
        return out;
    }

    bool chinese() const { return locale() != "en"; }

    std::vector<Row> rows(Browse browse) const
    {
        return buildRows(core().controller().snapshot(), browse, makeLang(chinese()));
    }

    std::vector<Row> rows() const { return rows(m_browse); }

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
    // The input the current list answers; a query for the same words is the host
    // asking again (a rerun) rather than the user typing something new.
    std::string m_answeredRaw;
};

// One serve loop, shared by the console binary and the DLL's worker thread.
inline int runServeLoop()
{
    return TioPlugin().serve();
}

} // namespace tioplug
