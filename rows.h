// rows.h - the search-box view model, with no Windows dependency.
//
// This is path A of the plugin. There is no window: WinShun draws the rows, and
// every parameter is edited in place. Two mechanisms carry that:
//
//   * effect::replace swaps the result list for a candidate list, and the host's
//     Esc returns to the previous one. That is how the port, baud-rate and
//     backend rows work, and it needs no protocol of its own.
//   * a value with a short cycle (data bits, parity, stop bits, flow) is changed
//     by the row's own action, then the row is re-answered from the new state.
//
// Nothing here localizes: the caller passes a `Lang` with the strings it wants,
// so the same row builders serve both languages without a second table. Only
// logic.h is included, so the tests can build and assert rows on any machine;
// the part that reads a Controller snapshot is in rows_build.h.
#pragma once

#include "logic.h"
#include "snapshot.h"

#include <string>
#include <utility>
#include <vector>

namespace tioplug {

// Which candidate list, if any, the current answer is showing. The host keeps the
// previous list for Esc; the plugin only needs to know which list it produced so
// the next query builds the same one.
enum class Browse { None, Ports, Baudrates, Backends, Diagnostics };

// The strings a row needs. Plain UTF-8 so the row builders hold no locale logic.
struct Lang {
    bool zh = true;
    const char* groupLaunch = "启动";
    const char* groupSessions = "会话（Enter 停止）";
    const char* groupParameters = "参数（Enter 修改，就地生效）";
    const char* groupInformation = "信息";
    const char* stop = "停止";
    const char* openWindow = "打开独立窗口";
    const char* openWindowNote = "同一个配置，多出身份采集、会话区块与保存";
    const char* port = "端口";
    const char* baudrate = "波特率";
    const char* dataBits = "数据位";
    const char* parity = "校验";
    const char* stopBits = "停止位";
    const char* flow = "流控";
    const char* backend = "终端后端";
    const char* control = "控制线 DTR / RTS";
    const char* controlNote = "默认行为未实测 · tio 无此 CLI 选项";
    const char* diagnostics = "诊断";
    const char* diagnosticsNote = "包版本、哈希、后端、会话；不含串口内容";
    const char* diagnosticsShow = "展开（Shift+Enter 复制）";
    const char* privacy = "不读串口内容";
    const char* noPorts = "（无可用端口）";
    const char* back = "返回";
    const char* blocked = "启动被阻塞";
    const char* running = "运行中";
    const char* starting = "启动中";
    const char* stopping = "停止中";
    const char* choice = "选择";
    const char* installed = "已安装";
    const char* notInstalled = "未安装或不在 PATH";
    const char* builtIn = "系统自带 · tio 是我们创建的子进程";
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
        case Gate::Parameters: why += lang.zh ? "参数" : "parameters"; break;
        case Gate::Identity: why += lang.zh ? "设备身份" : "identity"; break;
        case Gate::Package: why += lang.zh ? "完整性" : "integrity"; break;
        case Gate::Backend: why += lang.zh ? "后端" : "backend"; break;
        case Gate::Session: why += lang.zh ? "会话占用" : "session active"; break;
        }
    }
    return why;
}

// A group caption: drawn as a disabled row with no action. The id is fixed and
// language-independent, because the host keeps the keyboard on the row with the
// same id when the list is re-answered.
inline Row groupRow(const std::string& id, const std::string& caption)
{
    Row row;
    row.id = "group:" + id;
    row.title = caption;
    row.disabled = true;
    row.score = 1;
    return row;
}

// A row that carries the current value of one parameter; Enter opens its list or
// cycles it.
inline Row parameterRow(const std::string& id, const std::string& label,
    const std::string& value, const std::string& action)
{
    Row row;
    row.id = id;
    row.title = label;
    row.subtitle = value;
    row.icon = "glyph:U+E8B7";
    row.action = action;
    row.actionKey = "Enter";
    row.copy = value;
    row.score = 60;
    return row;
}

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

inline Row portRow(int com, const std::string& identity, bool current, const Lang& lang)
{
    Row row;
    row.id = "port:" + std::to_string(com);
    row.title = "COM" + std::to_string(com);
    row.subtitle = "COM" + std::to_string(com) + lang.arrow + Port::devicePath(com - 1)
        + lang.separator + identity;
    row.icon = "glyph:U+E8A7";
    row.action = "port";
    row.actionKey = "Enter";
    row.checked = current;
    row.score = 60;
    return row;
}

// The rates tio 3.9 is known to take; the list is closed rather than free text so
// a typo cannot reach argv.
inline const int* baudrates(int& count)
{
    static const int rates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1500000};
    count = static_cast<int>(sizeof rates / sizeof rates[0]);
    return rates;
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

// Both backends are listed. The one this machine lacks is disabled and says so,
// because a choice that cannot start is worse than no choice.
inline Row backendRow(Backend which, bool available, const std::string& version,
    bool current, const Lang& lang)
{
    Row row;
    row.action = "backend";
    row.actionKey = "Enter";
    row.checked = current;
    row.disabled = !available;
    if (which == Backend::Wt) {
        row.id = "backend:wt";
        row.title = "wt.exe — Windows Terminal";
        row.subtitle = available
            ? (version.empty() ? std::string(lang.installed)
                               : version + lang.separator + lang.installed)
            : std::string(lang.notInstalled);
        row.icon = "glyph:U+E756";
        row.score = 70;
        return row;
    }
    row.id = "backend:conhost";
    row.title = std::string("conhost.exe — ") + (lang.zh ? "系统控制台宿主" : "system console host");
    row.subtitle = available ? std::string(lang.builtIn) : std::string(lang.unavailable);
    row.icon = "glyph:U+E7F4";
    row.score = 69;
    return row;
}

// The diagnostics view: one read-only row per line of the report, so it can be
// read in the list rather than only copied. The first row carries the whole
// report for copying, because that is the path for pasting it somewhere.
//
// The report arrives as UTF-8 (what the controller produces); the caller widens
// it where the SDK needs wide text. Splitting is byte-wise on '\n', which is
// exact: no byte of a multi-byte UTF-8 sequence is 0x0A.
inline std::vector<Row> buildDiagnosticsRows(const std::string& report, const Lang& lang)
{
    std::vector<Row> rows;
    std::size_t at = 0;
    int index = 0;
    bool first = true;
    while (at < report.size()) {
        const std::size_t nl = report.find('\n', at);
        const std::string line = report.substr(at, nl == std::string::npos ? nl : nl - at);
        at = nl == std::string::npos ? report.size() : nl + 1;
        if (line.empty())
            continue;

        Row row;
        row.id = "diag:" + std::to_string(index++);
        row.title = line;
        row.icon = first ? "glyph:U+E9D9" : "color:#8a8a8a";
        row.disabled = true;
        row.score = 40;
        if (first) {
            row.copy = report;
            row.action = "copyDiagnostics";
            row.actionKey = "Shift+Enter";
        }
        first = false;
        rows.push_back(std::move(row));
    }
    (void)lang;
    return rows;
}

// ---- the cycle actions ----------------------------------------------------------
//
// A short list cycles in place; the caller persists the result. Kept here so the
// order is defined once and the tests can assert it.

inline void cycleDataBits(Serial& s)
{
    static const int values[] = {5, 6, 7, 8};
    for (std::size_t i = 0; i < 4; ++i) {
        if (values[i] == s.dataBits) {
            s.dataBits = values[(i + 1) % 4];
            return;
        }
    }
    s.dataBits = 8;
}

inline void cycleParity(Serial& s)
{
    static const Parity values[] = {Parity::None, Parity::Even, Parity::Odd, Parity::Mark, Parity::Space};
    for (std::size_t i = 0; i < 5; ++i) {
        if (values[i] == s.parity) {
            s.parity = values[(i + 1) % 5];
            return;
        }
    }
    s.parity = Parity::None;
}

inline void cycleStopBits(Serial& s) { s.stopBits = s.stopBits == 1 ? 2 : 1; }

inline void cycleFlow(Serial& s)
{
    static const Flow values[] = {Flow::None, Flow::Hard, Flow::Soft};
    for (std::size_t i = 0; i < 3; ++i) {
        if (values[i] == s.flow) {
            s.flow = values[(i + 1) % 3];
            return;
        }
    }
    s.flow = Flow::None;
}

} // namespace tioplug
