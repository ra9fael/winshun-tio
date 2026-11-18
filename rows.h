// rows.h - the search-box view model, with no Windows dependency.
//
// This is path A of the plugin. There is no window: WinShun draws the rows, and
// the list stays lean — one launch row per port, the baud-rate shortcuts, and
// one row that shows the whole configuration and opens the standalone window.
// The baud list swaps in with effect::replace and the host's Esc goes back; the
// window holds every other parameter.
//
// Nothing here localizes: the caller passes a `Lang` with the strings it wants,
// so the same row builders serve both languages without a second table. Only
// logic.h is included, so the tests can build and assert rows on any machine;
// the part that reads a Controller snapshot is in rows_build.h.
#pragma once

#include "logic.h"
#include "snapshot.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace tioplug {

// Which candidate list, if any, the current answer is showing. The host keeps the
// previous list for Esc; the plugin only needs to know which list it produced so
// the next query builds the same one.
enum class Browse { None, Baudrates };

// The strings a row needs. Plain UTF-8 so the row builders hold no locale logic.
struct Lang {
    bool zh = true;
    const char* openWindow = "打开独立窗口";
    const char* baudrate = "波特率";
    const char* automatic = "自动";
    const char* backend = "终端后端";
    const char* noPorts = "（无可用端口）";
    const char* back = "返回";
    const char* blocked = "启动被阻塞";
    const char* running = "运行中";
    const char* starting = "启动中";
    const char* stopping = "停止中";
    const char* unavailable = "不可用";
    const char* tioDefault = "tio 默认";
    const char* separator = " · ";
    const char* arrow = " → ";
};

// One row before it becomes JSON. main.cpp turns this into a winshun::Item so
// this header stays free of the SDK's item type and can be unit tested.
struct Row {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string icon;      // "glyph:U+E8B7" or "color:#…"
    int score = 50;
    std::string action;    // action id, empty when the row does nothing
    std::string actionKey; // "Enter"
    // Extra actions the host lists in the row's menu (Ctrl+Enter and the rest).
    // The first is `action`/`actionKey` above; these follow it.
    std::vector<std::pair<std::string, std::string>> moreActions; // id, key
    bool disabled = false;
    bool checked = false;  // shows a tick, for a candidate list
    std::string copy;      // Shift+Enter copies this when set
};

// ---- small formatting helpers ---------------------------------------------------

// 115200 8n1 · none — the same spelling the window shows.
inline std::string serialText(const Serial& s)
{
    std::string text = std::to_string(s.baudrate) + " " + std::to_string(s.dataBits);
    text += s.parity == Parity::None ? "n" : std::string(1, parityName(s.parity)[0]);
    text += std::to_string(s.stopBits);
    text += " · " + std::string(flowName(s.flow));
    return text;
}

inline std::string backendLabel(Backend backend, const Lang& lang)
{
    switch (backend) {
    case Backend::Wt: return "Windows Terminal";
    case Backend::Conhost: return lang.zh ? "conhost 控制台" : "conhost";
    case Backend::None: break;
    }
    return lang.unavailable;
}

// The gates that stop a launch, spelled out so the launch row can say why it is
// grey instead of going quietly dead.
inline std::string gateText(const Readiness& readiness, const Lang& lang)
{
    std::string why;
    for (const Gate gate : readiness.blocking) {
        if (!why.empty())
            why += lang.separator;
        switch (gate) {
        case Gate::Port: why += lang.zh ? "端口" : "port"; break;
        case Gate::PortBusy: why += lang.zh ? "端口已被占用" : "port busy"; break;
        case Gate::SharePort: why += lang.zh ? "共享端口需为 1–65535" : "shared port must be 1–65535"; break;
        case Gate::Parameters: why += lang.zh ? "参数" : "parameters"; break;
        case Gate::Identity: why += lang.zh ? "设备身份" : "identity"; break;
        case Gate::Package: why += lang.zh ? "完整性" : "integrity"; break;
        case Gate::Backend: why += lang.zh ? "后端" : "backend"; break;
        case Gate::Session: why += lang.zh ? "会话占用" : "session active"; break;
        }
    }
    return why;
}

// A row that carries the current value of one parameter; Enter opens its list or
// cycles it.
inline Row backRow(const Lang& lang)
{
    Row row;
    row.id = "back";
    row.title = lang.back;
    row.icon = "glyph:U+E72B";
    row.action = "back";
    row.actionKey = "Enter";
    row.score = 100;
    return row;
}

// ---- candidate lists ------------------------------------------------------------
//
// These take only the values they show, so both entry points and the tests can
// build them without a session or a package.

// The rates the pinned tio build is known to take. The list is what the search
// box offers as shortcuts; a rate outside it is still a valid configuration (the
// window takes one), so the list carries the current value even when it is not
// one of the usual ones.
inline const int* baudrates(int& count)
{
    static const int rates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1500000};
    count = static_cast<int>(sizeof rates / sizeof rates[0]);
    return rates;
}

// The rate list for a form: the usual ones, plus the current rate when the user
// set a custom one, so the value on screen is always one of the rows.
inline std::vector<int> baudrateChoices(int current)
{
    int count = 0;
    const int* rates = baudrates(count);
    std::vector<int> out(rates, rates + count);
    if (std::find(out.begin(), out.end(), current) == out.end())
        out.push_back(current);
    return out;
}

inline Row baudRow(int rate, bool current, const Lang& lang)
{
    Row row;
    row.id = "baud:" + std::to_string(rate);
    row.title = std::to_string(rate);
    row.subtitle = rate == 115200 ? std::string(lang.tioDefault) : std::string();
    row.icon = "glyph:U+E9D9";
    row.action = "baud";
    row.actionKey = "Enter";
    row.checked = current;
    row.score = 60;
    return row;
}

// "自动（Windows Terminal）" or the pinned backend: the search box says which
// choice is in force and which terminal it resolves to.
inline std::string backendChoiceLabel(BackendPreference preference, Backend resolved, const Lang& lang)
{
    if (preference == BackendPreference::Auto)
        return std::string(lang.automatic) + (lang.zh ? "（" : " (") + backendLabel(resolved, lang)
            + (lang.zh ? "）" : ")");
    return backendLabel(resolved, lang);
}

} // namespace tioplug
