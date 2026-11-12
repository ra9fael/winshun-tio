// test_logic.cpp - the pure rules in logic.h, with no Windows dependency.
// Follows the ra9fael.shell pattern: a CHECK macro, one main, a summary line.
#include "../core.h"
#include "../logic.h"
#include "../rows.h"
#include "../rows_build.h"
#include "../winshun.h"

#include <cstdio>
#include <string>
#include <vector>
int g_total = 0;
int g_failed = 0;

#define CHECK(condition, label)                                        \
    do {                                                               \
        ++g_total;                                                     \
        if (!(condition)) {                                            \
            ++g_failed;                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, label); \
        }                                                              \
    } while (0)

// The same, with the actual value appended: for a check whose failure needs it.
#define CHECK_D(condition, label, detail)                                            \
    do {                                                                             \
        ++g_total;                                                                   \
        if (!(condition)) {                                                          \
            ++g_failed;                                                              \
            std::printf("FAIL %s:%d  %s (%s)\n", __FILE__, __LINE__, label,          \
                std::string(detail).c_str());                                        \
        }                                                                            \
    } while (0)

using namespace tioplug;

// ---- T-T03: COMn -> /dev/ttyS(n-1) ---------------------------------------------

static void testPortMapping()
{
    const Port one = parsePort("COM1");
    CHECK(one.ok(), "COM1 parses");
    CHECK(one.com == 1, "COM1 number");
    CHECK(one.index == 0, "COM1 maps to index 0");
    CHECK(one.device() == "/dev/ttyS0", "COM1 -> /dev/ttyS0");

    const Port ten = parsePort("COM10");
    CHECK(ten.ok() && ten.device() == "/dev/ttyS9", "COM10 -> /dev/ttyS9");

    const Port max = parsePort("COM256");
    CHECK(max.ok(), "COM256 parses");
    CHECK(max.index == 255, "COM256 index 255");
    CHECK(max.device() == "/dev/ttyS255", "COM256 -> /dev/ttyS255");

    // Case and padding are accepted; the displayed form comes from `com`.
    CHECK(parsePort("com7").device() == "/dev/ttyS6", "lowercase accepted");
    CHECK(parsePort("Com 7").device() == "/dev/ttyS6", "space accepted");
    CHECK(parsePort("  COM7  ").device() == "/dev/ttyS6", "outer space trimmed");
    CHECK(parsePort("COM007").com == 7, "leading zeros");
    CHECK(parsePort("COM007").device() == "/dev/ttyS6", "leading zeros map the same");
}

static void testPortRejection()
{
    CHECK(parsePort("").error == PortError::Empty, "empty");
    CHECK(parsePort("   ").error == PortError::Empty, "blank");
    CHECK(parsePort("COM").error == PortError::Syntax, "no digits");
    CHECK(parsePort("COM  ").error == PortError::Syntax, "no digits after space");
    CHECK(parsePort("ttyS0").error == PortError::Syntax, "bare device name rejected");
    CHECK(parsePort("/dev/ttyS0").error == PortError::Syntax, "unix path rejected");
    CHECK(parsePort("COM1; rm -rf /").error == PortError::NotInteger, "injection rejected");
    CHECK(parsePort("COM10 && calc").error == PortError::NotInteger, "shell injection rejected");
    CHECK(parsePort("COM0").error == PortError::NotPositive, "COM0 rejected");
    CHECK(parsePort("COM-1").error == PortError::Syntax, "negative rejected");
    CHECK(parsePort("COM+1").error == PortError::Syntax, "explicit plus rejected");
    CHECK(parsePort("COM1.5").error == PortError::NotInteger, "fraction rejected");
    CHECK(parsePort("COM257").error == PortError::Overflow, "above 256 rejected");
    CHECK(parsePort("COM999999").error == PortError::Overflow, "far above 256 rejected");
    // A long digit run must saturate rather than overflow into a bogus accept.
    CHECK(parsePort("COM99999999999999999999").error == PortError::Overflow, "huge number rejected");
    CHECK(!parsePort("COM0").ok(), "rejected port is not ok");
    CHECK(parsePort("COM0").device().empty(), "rejected port has no device");
    CHECK(parsePort("COM257").index == -1, "rejected port has no index");
}

// ---- T-T04: serial parameters ---------------------------------------------------

static void testParameters()
{
    Serial s;
    CHECK(validate(s) == ParamError::None, "defaults are valid");
    CHECK(s.baudrate == 115200, "default baudrate");
    CHECK(s.dataBits == 8, "default data bits");
    CHECK(s.stopBits == 1, "default stop bits");
    CHECK(std::string(parityName(s.parity)) == "none", "default parity");
    CHECK(std::string(flowName(s.flow)) == "none", "default flow");

    s.baudrate = 0;
    CHECK(validate(s) == ParamError::Baudrate, "zero baudrate rejected");
    s.baudrate = -9600;
    CHECK(validate(s) == ParamError::Baudrate, "negative baudrate rejected");
    s.baudrate = 1500000;
    CHECK(validate(s) == ParamError::None, "high baudrate accepted");

    s.dataBits = 4;
    CHECK(validate(s) == ParamError::DataBits, "4 data bits rejected");
    s.dataBits = 9;
    CHECK(validate(s) == ParamError::DataBits, "9 data bits rejected");
    for (int bits = 5; bits <= 8; ++bits) {
        s.dataBits = bits;
        CHECK(validate(s) == ParamError::None, "5..8 data bits accepted");
    }

    s.stopBits = 3;
    CHECK(validate(s) == ParamError::StopBits, "3 stop bits rejected");
    s.stopBits = 2;
    CHECK(validate(s) == ParamError::None, "2 stop bits accepted");

    Parity parity = Parity::None;
    CHECK(parseParity("mark", parity) && parity == Parity::Mark, "mark parity");
    CHECK(parseParity("space", parity) && parity == Parity::Space, "space parity");
    CHECK(!parseParity("MARK", parity), "parity is case sensitive like the CLI");
    CHECK(!parseParity("", parity), "empty parity rejected");

    Flow flow = Flow::None;
    CHECK(parseFlow("hard", flow) && flow == Flow::Hard, "hard flow");
    CHECK(parseFlow("soft", flow) && flow == Flow::Soft, "soft flow");
    CHECK(!parseFlow("xon", flow), "unknown flow rejected");
}

// ---- T-T04: argv quoting --------------------------------------------------------

static void testQuoting()
{
    CHECK(quoteArg("plain") == "plain", "no quotes needed");
    CHECK(quoteArg("") == "\"\"", "empty argument");
    CHECK(quoteArg("two words") == "\"two words\"", "space forces quotes");
    CHECK(quoteArg("tab\there") == "\"tab\there\"", "tab forces quotes");

    // A quote is escaped and every backslash before it doubles.
    CHECK(quoteArg("say \"hi\"") == "\"say \\\"hi\\\"\"", "inner quotes escaped");
    CHECK(quoteArg("C:\\dir\\") == "C:\\dir\\", "trailing backslash without quotes");
    CHECK(quoteArg("C:\\my dir\\") == "\"C:\\my dir\\\\\"", "trailing backslash doubled");

    // UTF-8 passes through unchanged; a space inside CJK text still quotes.
    const std::string cjk = "D:\\工具\\串口 终端\\tio.exe";
    CHECK(quoteArg(cjk) == "\"" + cjk + "\"", "unicode path quoted verbatim");
    const std::string noSpace = "D:\\工具\\串口\\tio.exe";
    CHECK(quoteArg(noSpace) == noSpace, "unicode path without space unquoted");

    const std::string line = buildCommandLine({"tio.exe", "-b", "115200", "/dev/ttyS9"});
    CHECK(line == "tio.exe -b 115200 /dev/ttyS9", "command line join");

    // An empty argument survives as "" rather than vanishing.
    CHECK(buildCommandLine({"a", "", "b"}) == "a \"\" b", "empty argument kept");
}

// ---- TIO-005: backend choice ----------------------------------------------------

static void testBackends()
{
    Backends found;
    CHECK(preferredBackend(found) == Backend::None, "nothing installed -> none");

    found.conhost = true;
    CHECK(preferredBackend(found) == Backend::Conhost, "conhost only -> conhost");

    found.wt = true;
    found.wtVersion = "1.25.2733";
    CHECK(preferredBackend(found) == Backend::Wt, "wt preferred over conhost");
    CHECK(available(found, Backend::Wt), "wt available");
    CHECK(available(found, Backend::Conhost), "conhost available");

    // A saved choice stands while its backend exists.
    bool changed = true;
    CHECK(resolveBackend(found, Backend::Conhost, &changed) == Backend::Conhost, "saved conhost kept");
    CHECK(!changed, "no change to persist");
    CHECK(resolveBackend(found, Backend::Wt, &changed) == Backend::Wt, "saved wt kept");

    // wt disappeared: the saved choice no longer holds, and that must be saved.
    found.wt = false;
    changed = false;
    CHECK(resolveBackend(found, Backend::Wt, &changed) == Backend::Conhost, "fall back to conhost");
    CHECK(changed, "fallback must be persisted");

    found.conhost = false;
    changed = false;
    CHECK(resolveBackend(found, Backend::Wt, &changed) == Backend::None, "both gone -> none");
    CHECK(changed, "losing both is also a change");
    CHECK(!available(found, Backend::Wt), "wt gone");

    CHECK(std::string(backendId(Backend::Wt)) == "wt", "backend id wt");
    CHECK(std::string(backendId(Backend::Conhost)) == "conhost", "backend id conhost");
    CHECK(backendFromId("wt") == Backend::Wt, "backend from id wt");
    CHECK(backendFromId("nope") == Backend::None, "unknown backend id");

    CHECK(pathFitsWt("D:\\plugins\\ra9fael.tio\\runtime\\tio.exe"), "ordinary path fits wt");
    CHECK(pathFitsWt("D:\\工具\\串口\\tio.exe"), "unicode path fits wt");
    CHECK(!pathFitsWt("D:\\a;b\\tio.exe"), "semicolon path refused");
    CHECK(!pathFitsWt("D:\\a|b\\tio.exe"), "pipe path refused");
}

// ---- launch plans ---------------------------------------------------------------

static void testPlans()
{
    const Port port = parsePort("COM10");
    const Serial serial;

    const Plan conhost = planLaunch(Backend::Conhost, "wt.exe", "D:\\p\\tio.exe", port, serial, "s-abc123");
    CHECK(conhost.error == PlanError::None, "conhost plan is valid");
    CHECK(conhost.program.size() == 13, "conhost plan is tio plus twelve arguments");
    CHECK(conhost.program.front() == "D:\\p\\tio.exe", "conhost runs tio first");
    CHECK(conhost.title.empty(), "conhost has no tab title");

    // tio's own arguments: -n, then the parameter pairs, then the device.
    CHECK(conhost.program[1] == "-n", "-n disables tio's own reconnect");
    CHECK(conhost.program[2] == "-b" && conhost.program[3] == "115200", "baudrate pair");
    CHECK(conhost.program[4] == "-d" && conhost.program[5] == "8", "data bits pair");
    CHECK(conhost.program[6] == "-p" && conhost.program[7] == "none", "parity pair");
    CHECK(conhost.program[8] == "-s" && conhost.program[9] == "1", "stop bits pair");
    CHECK(conhost.program[10] == "-f" && conhost.program[11] == "none", "flow pair");
    CHECK(conhost.program.back() == "/dev/ttyS9", "device is last");

    const Plan wt = planLaunch(Backend::Wt, "C:\\wt\\wt.exe", "D:\\p\\tio.exe", port, serial, "s-abc123");
    CHECK(wt.error == PlanError::None, "wt plan is valid");
    CHECK(wt.program[0] == "C:\\wt\\wt.exe", "wt runs wt first");
    CHECK(wt.program[1] == "-w" && wt.program[2] == "new", "wt forced to a new window");
    CHECK(wt.program[3] == "--title" && wt.program[4] == "tio COM10 s-abc123", "title carries the token");
    CHECK(wt.title == "tio COM10 s-abc123", "title exposed for the session record");
    CHECK(wt.program[5] == "--suppressApplicationTitle", "tio cannot rename the tab");
    CHECK(wt.program.size() == 6 + conhost.program.size(), "wt wraps the tio arguments");
    CHECK(wt.program[6] == "D:\\p\\tio.exe", "tio follows the wt options");

    // Gates that must fail before a command line exists.
    Serial broken;
    broken.dataBits = 12;
    const Plan badParams = planLaunch(Backend::Conhost, "", "tio.exe", port, broken, "s-1");
    CHECK(badParams.error == PlanError::Parameters, "bad parameters are caught in the plan");

    const Plan badPort = planLaunch(Backend::Conhost, "", "tio.exe", parsePort("COM0"), serial, "s-1");
    CHECK(badPort.error == PlanError::Port, "bad port is caught in the plan");

    const Plan noBackend = planLaunch(Backend::None, "", "tio.exe", port, serial, "s-1");
    CHECK(noBackend.error != PlanError::None, "no backend cannot produce a plan");

    // A package path that wt would split cannot be launched through wt.
    const Plan semicolon = planLaunch(Backend::Wt, "wt.exe", "D:\\a;b\\tio.exe", port, serial, "s-1");
    CHECK(semicolon.error == PlanError::PackagePath, "semicolon path refused by wt plan");
    // The same path is fine for conhost, which takes one command line.
    const Plan semicolonConhost = planLaunch(Backend::Conhost, "", "D:\\a;b\\tio.exe", port, serial, "s-1");
    CHECK(semicolonConhost.error == PlanError::None, "semicolon path fine for conhost");

    // Different tokens give different titles, so two sessions cannot collide.
    const Plan other = planLaunch(Backend::Wt, "wt.exe", "tio.exe", port, serial, "s-def456");
    CHECK(other.title != wt.title, "tokens keep titles distinct");

    // A serial override reaches the command line in the CLI's own spelling.
    Serial odd;
    odd.baudrate = 9600;
    odd.dataBits = 7;
    odd.parity = Parity::Odd;
    odd.stopBits = 2;
    odd.flow = Flow::Hard;
    const Plan custom = planLaunch(Backend::Conhost, "", "tio.exe", port, odd, "s-1");
    CHECK(custom.program[3] == "9600", "custom baudrate reaches argv");
    CHECK(custom.program[5] == "7", "custom data bits reaches argv");
    CHECK(custom.program[7] == "odd", "custom parity reaches argv");
    CHECK(custom.program[9] == "2", "custom stop bits reaches argv");
    CHECK(custom.program[11] == "hard", "custom flow reaches argv");

    // Capture is tio writing its own log: the flags go after the parameters and
    // before the device, so the device stays last. Without a log path tio gets
    // exactly the arguments it always got.
    const Plan capture = planLaunch(Backend::Conhost, "", "tio.exe", port, serial, "s-1",
                                    "D:\\data\\sessions\\s-1.log");
    CHECK(capture.error == PlanError::None, "a capture plan is valid");
    CHECK(capture.program.size() == conhost.program.size() + 5, "capture adds five arguments");
    CHECK(capture.program[12] == "-L", "logging is switched on");
    CHECK(capture.program[13] == "--log-file", "the log file flag follows");
    CHECK(capture.program[14] == "D:\\data\\sessions\\s-1.log", "the log path is the one given");
    CHECK(capture.program[15] == "--log-append", "the log appends");
    CHECK(capture.program[16] == "--log-strip", "escape sequences are stripped");
    CHECK(capture.program.back() == "/dev/ttyS9", "the device stays last with capture on");
    CHECK(conhost.program.back() == "/dev/ttyS9", "and last without it");
}

// ---- session state machine ------------------------------------------------------

static void testSession()
{
    Session s;
    CHECK(s.phase == Phase::Configuring, "starts configuring");
    CHECK(!s.active(), "configuring is not active");
    CHECK(std::string(phaseId(s.phase)) == "configuring", "phase id");
    CHECK(std::string(portStateId(s.port)) == "unknown", "port starts unknown");
    CHECK(!ownedProcess(s, 100, 1), "no process owned before start");

    CHECK(!beginStart(s), "cannot start from configuring");
    CHECK(beginValidation(s), "validation begins");
    CHECK(!beginValidation(s), "validation cannot restart while running");
    CHECK(s.busy(), "validating counts as busy");

    CHECK(beginStart(s), "start after validation");
    CHECK(s.phase == Phase::Starting, "phase is starting");
    CHECK(!markRunning(s, 0, 0), "a zero PID is not a running session");
    CHECK(markRunning(s, 8842, 133000000000000000ull), "running with a real PID");
    CHECK(s.phase == Phase::Running, "phase is running");
    CHECK(s.pid == 8842, "PID recorded");
    CHECK(ownedProcess(s, 8842, 133000000000000000ull), "creation time confirms ownership");
    CHECK(!ownedProcess(s, 8842, 133000000000000001ull), "reused PID is not ours");
    CHECK(!ownedProcess(s, 9999, 133000000000000000ull), "other PID is not ours");
    CHECK(s.port == PortState::Unknown, "running does not imply the port is open");

    // A second start cannot run alongside the first.
    CHECK(!beginValidation(s), "no second session while running");

    CHECK(beginStop(s), "stop begins");
    CHECK(!beginStop(s), "stop is not re-entrant");
    CHECK(s.phase == Phase::Stopping, "phase is stopping");

    CHECK(markExited(s), "exit after stopping");
    CHECK(s.phase == Phase::Exited, "phase is exited");
    CHECK(s.pid == 0, "PID cleared on exit");
    CHECK(s.created == 0, "creation time cleared on exit");
    CHECK(!markExited(s), "cannot exit twice");

    // Restart after an exit is allowed, and the next session starts clear.
    CHECK(beginValidation(s), "restart after exit");
    CHECK(beginStart(s), "start again");
}

static void testSessionFailure()
{
    Session s;
    beginValidation(s);
    beginStart(s);
    // The process never appeared (busy port, missing binary).
    CHECK(markFailed(s, PortState::Busy), "failure from starting");
    CHECK(s.phase == Phase::Failed, "phase is failed");
    CHECK(s.port == PortState::Busy, "busy reason recorded");
    CHECK(s.pid == 0, "no PID on a failed start");

    // A failure cannot overwrite an exit already recorded.
    Session t;
    beginValidation(t);
    beginStart(t);
    markRunning(t, 100, 7);
    beginStop(t);
    markExited(t);
    CHECK(!markFailed(t, PortState::Gone), "a finished session is not failed again");

    // Device removal voids the session even while it is running.
    Session u;
    beginValidation(u);
    beginStart(u);
    markRunning(u, 200, 9);
    CHECK(markRemoved(u), "removal voids a running session");
    CHECK(u.phase == Phase::Removed, "phase is removed");
    CHECK(u.port == PortState::Gone, "port state is gone");
    CHECK(!markRemoved(u), "removal is not re-entrant");
    CHECK(!markFailed(u, PortState::Gone), "removal is final");
}

// ---- readiness gates ------------------------------------------------------------

static void testReadiness()
{
    Environment env;
    env.port = parsePort("COM10");
    env.backend = Backend::Wt;
    CHECK(env.serial.baudrate == 115200, "environment has default parameters");

    const Readiness ready = check(env);
    CHECK(ready.ready(), "a complete environment is ready");
    CHECK(ready.blocking.empty(), "no blocking gate");

    // Each gate on its own, so the reported reason is unambiguous.
    Environment badPort = env;
    badPort.port = parsePort("COM0");
    const Readiness r1 = check(badPort);
    CHECK(r1.blocking.size() == 1 && r1.blocking[0] == Gate::Port, "port gate");

    Environment badSerial = env;
    badSerial.serial.dataBits = 9;
    const Readiness r2 = check(badSerial);
    CHECK(r2.blocking.size() == 1 && r2.blocking[0] == Gate::Parameters, "parameters gate");

    Environment noIdentity = env;
    noIdentity.identityKnown = false;
    const Readiness r3 = check(noIdentity);
    CHECK(r3.blocking.size() == 1 && r3.blocking[0] == Gate::Identity, "identity gate");

    Environment badPackage = env;
    badPackage.packageValid = false;
    const Readiness r4 = check(badPackage);
    CHECK(r4.blocking.size() == 1 && r4.blocking[0] == Gate::Package, "package gate");

    Environment noBackend = env;
    noBackend.backend = Backend::None;
    const Readiness r5 = check(noBackend);
    CHECK(r5.blocking.size() == 1 && r5.blocking[0] == Gate::Backend, "backend gate");

    Environment running = env;
    running.sessionActive = true;
    const Readiness r6 = check(running);
    CHECK(r6.blocking.size() == 1 && r6.blocking[0] == Gate::Session, "session gate");

    // A port another session runs on is refused by name: the reason is not "the
    // port is bad", it is "someone else has it right now".
    Environment busy = env;
    busy.portBusyByOther = true;
    const Readiness r7 = check(busy);
    CHECK(r7.blocking.size() == 1 && r7.blocking[0] == Gate::PortBusy, "port busy gate");
    CHECK(std::string(gateId(r7.blocking[0])) == "portBusy", "port busy gate id");

    // A port that does not parse is not also called busy: one reason, not two.
    Environment invalidAndBusy = badPort;
    invalidAndBusy.portBusyByOther = true;
    const Readiness r8 = check(invalidAndBusy);
    CHECK(r8.blocking.size() == 1 && r8.blocking[0] == Gate::Port,
          "an invalid port is not also called busy");

    // Everything broken at once: the order is stable so the window names the
    // same first thing to fix on every reload. A busy port needs a valid port
    // to be busy, so this set holds six.
    Environment all;
    all.port = parsePort("COM0");
    all.serial.dataBits = 9;
    all.identityKnown = false;
    all.packageValid = false;
    all.backend = Backend::None;
    all.sessionActive = true;
    all.portBusyByOther = true;
    const Readiness full = check(all);
    CHECK(full.blocking.size() == 6, "all six gates block");
    CHECK(full.blocking[0] == Gate::Port, "port reported first");
    CHECK(full.blocking[1] == Gate::Parameters, "parameters second");
    CHECK(full.blocking[2] == Gate::Identity, "identity third");
    CHECK(full.blocking[3] == Gate::Package, "package fourth");
    CHECK(full.blocking[4] == Gate::Backend, "backend fifth");
    CHECK(full.blocking[5] == Gate::Session, "session last");
    CHECK(std::string(gateId(full.blocking[0])) == "port", "gate id");
    CHECK(!full.ready(), "all broken is not ready");
}

// ---- T-T11: the search-box rows (path A) ---------------------------------------

static void testRows()
{
    Lang lang;
    CHECK(std::string(lang.separator) == " · ", "the separator is a middle dot");
    CHECK(std::string(lang.arrow) == " → ", "the arrow marks the mapping");

    // A group caption's id is fixed so the keyboard stays on the same row when
    // the host re-answers in another language.
    const Row group = groupRow("launch", lang.groupLaunch);
    CHECK_D(group.id == "group:launch", "a group row id is language-independent", group.id);
    const Row groupEn = groupRow("launch", "Launch");
    CHECK(groupEn.id == group.id, "the same group id in either language");
    CHECK(group.disabled, "a group caption is not selectable");

    // serialText: the same spelling everywhere.
    Serial s;
    CHECK(serialText(s) == "115200 8n1 · none", "the default serial text");
    s.parity = Parity::Even;
    s.stopBits = 2;
    s.flow = Flow::Hard;
    CHECK(serialText(s) == "115200 8e2 · hard", "an odd setting round trips");

    // Backend labels say which one is theirs; an absent backend says so.
    CHECK(backendLabel(Backend::Wt, lang) == "Windows Terminal", "wt label");
    CHECK(backendLabel(Backend::Conhost, lang) == "conhost 控制台", "conhost label zh");
    Lang en;
    en.zh = false;
    CHECK(backendLabel(Backend::Conhost, en) == "conhost", "conhost label en");
    CHECK(backendLabel(Backend::None, lang) == "不可用", "no backend label");

    // A blocked launch row names the failing gates in the fixed order.
    Environment env;
    env.port = parsePort("COM10");
    env.backend = Backend::Wt;
    CHECK(gateText(check(env), lang).empty(), "no gate text when ready");
    Environment bad = env;
    bad.port = parsePort("COM0");
    bad.packageValid = false;
    const std::string why = gateText(check(bad), lang);
    CHECK_D(why == "端口 · 完整性", "the gate text follows the gate order", why);

    // A candidate list always starts with back, and only one row is ticked.
    Row back = backRow(lang);
    CHECK(back.id == "back", "the back row id");
    CHECK(back.action == "back" && back.actionKey == "Enter", "back is on Enter");
    CHECK(back.score > 99, "back sorts first");

    Row current = portRow(10, "USB\\VID_10C4&PID_EA60\\0001", true, lang);
    CHECK(current.id == "port:10", "a port row id names the port");
    CHECK(current.title == "COM10", "a port row titles the COMn");
    CHECK_D(current.subtitle.find("/dev/ttyS9") != std::string::npos,
        "a port row shows the n-1 mapping", current.subtitle);
    CHECK_D(current.subtitle.find("VID_10C4") != std::string::npos,
        "a port row shows the device identity", current.subtitle);
    CHECK(current.checked, "the current port is ticked");
    CHECK(!portRow(9, "", false, lang).checked, "another port is not ticked");

    // The baud list is closed, and only 115200 is called the default.
    int count = 0;
    const int* rates = baudrates(count);
    CHECK_D(count >= 5, "the baud list has several rates", std::to_string(count));
    bool foundDefault = false;
    for (int i = 0; i < count; ++i) {
        if (rates[i] == 115200)
            foundDefault = true;
    }
    CHECK(foundDefault, "115200 is in the baud list");
    CHECK(baudRow(115200, true, lang).subtitle == "tio 默认", "115200 is the tio default");
    CHECK(baudRow(9600, false, lang).subtitle.empty(), "another rate carries no note");

    // A backend the machine lacks is disabled but still listed: a choice that
    // cannot start is worse than no choice.
    const Row missing = backendRow(Backend::Wt, false, std::string(), true, lang);
    CHECK(missing.disabled, "an absent backend is disabled");
    CHECK(!missing.checked || missing.disabled, "disabled wins over the tick");
    CHECK_D(missing.subtitle.find("未安装") != std::string::npos, "it says it is not installed",
        missing.subtitle);
    const Row installed = backendRow(Backend::Wt, true, "1.25.2733.0", false, lang);
    CHECK(!installed.disabled, "a present backend is selectable");
    CHECK_D(installed.subtitle.find("1.25.2733.0") != std::string::npos,
        "it names the version", installed.subtitle);
    CHECK(backendRow(Backend::Conhost, true, std::string(), false, lang).score
            < backendRow(Backend::Wt, true, std::string(), false, lang).score,
        "wt sorts before conhost");

    // The cycle order is the one the UI promises, and it wraps.
    Serial cycle;    cycleDataBits(cycle);
    CHECK(cycle.dataBits == 5, "8 cycles to 5");
    for (int i = 0; i < 3; ++i)
        cycleDataBits(cycle);
    CHECK(cycle.dataBits == 8, "three more come back to 8");
    cycle.dataBits = 9; // not a supported value
    cycleDataBits(cycle);
    CHECK(cycle.dataBits == 8, "an unsupported value resets to 8");

    cycle.parity = Parity::None;
    cycleParity(cycle);
    CHECK(cycle.parity == Parity::Even, "none cycles to even");
    for (int i = 0; i < 4; ++i)
        cycleParity(cycle);
    CHECK(cycle.parity == Parity::None, "five cycles come back to none");

    cycle.stopBits = 1;
    cycleStopBits(cycle);
    CHECK(cycle.stopBits == 2, "1 stop bit cycles to 2");
    cycleStopBits(cycle);
    CHECK(cycle.stopBits == 1, "and back to 1");

    cycle.flow = Flow::None;
    cycleFlow(cycle);
    CHECK(cycle.flow == Flow::Hard, "none cycles to hard");
    cycleFlow(cycle);
    CHECK(cycle.flow == Flow::Soft, "hard cycles to soft");
    cycleFlow(cycle);
    CHECK(cycle.flow == Flow::None, "soft cycles to none");
    cycle.flow = Flow::Hard;
    cycleFlow(cycle);
    CHECK(validate(cycle) == ParamError::None, "a cycled form always validates");
}

// The diagnostics view: one read-only row per line, the whole report on the
// first row's copy, no row claims a connection.
static void testDiagnosticsView()
{
    Lang lang;

    // The report is what the controller would show; the row builder only splits
    // it, so a hand-written block stands in for the controller here.
    const std::string report =
        "ra9fael.tio launcher\n"
        "package   : 3.9-3\n"
        "integrity : valid\n"
        "backend   : wt (wt 1.25.2733.0)\n"
        "port      : COM10 -> /dev/ttyS9\n"
        "session   : configuring\n"
        "privacy   : no serial payload is read or logged; the port path is kept out of shared logs\n";

    const std::vector<Row> view = buildDiagnosticsRows(report, lang);
    CHECK(view.size() == 7, "one row per report line", std::to_string(view.size()));
    CHECK(!view.empty() && view.front().id == "diag:0", "the first row is diag:0");
    CHECK(view.front().title == "ra9fael.tio launcher", "the first row is the header");
    CHECK(view.back().title.find("privacy") != std::string::npos, "the last row is the privacy line");

    for (const Row& row : view)
        CHECK(row.disabled, "a diagnostics row is read-only", row.id);

    // The whole report travels on the first row only.
    CHECK(view.front().copy == report, "the first row copies the report");
    CHECK(view.front().action == "copyDiagnostics", "the first row offers copy on Shift+Enter");
    for (std::size_t i = 1; i < view.size(); ++i) {
        CHECK(view[i].copy.empty(), "no other row carries a copy", view[i].id);
        CHECK(view[i].action.empty(), "no other row has an action", view[i].id);
    }

    // A row never claims the port is open.
    for (const Row& row : view) {
        CHECK(row.title.find("connected") == std::string::npos,
            "a diagnostics row does not claim a connection", row.id);
    }

    // An empty report produces no rows: nothing to read, nothing to offer.
    CHECK(buildDiagnosticsRows(std::string(), lang).empty(), "an empty report has no rows");
}

// The standalone window's entries: its own row, and Ctrl+Enter on the launch
// row. The user has to be able to see a way in, not just know a chord.
static void testWindowEntry()
{
    Lang lang;
    CHECK(std::string(lang.openWindow) == "打开独立窗口", "the entry row's title");

    // The row: its own, with the entry action on Enter.
    Snapshot s;
    s.port = parsePort("COM10");
    s.readiness.blocking.push_back(Gate::Port); // blocked: the entry must still be there
    const std::vector<Row> rows = buildRows(s, Browse::None, lang);
    const Row* entry = nullptr;
    for (const Row& row : rows) {
        if (row.id == "openWindow")
            entry = &row;
    }
    CHECK(entry != nullptr, "the window entry row is in the list");
    if (entry) {
        CHECK(entry->action == "openWindow", "the entry row opens the window", entry->action);
        CHECK(entry->actionKey == "Enter", "the entry row is on Enter");
        CHECK(!entry->disabled, "the entry row is selectable even when launch is blocked");
        CHECK(entry->score > 50, "the entry row sorts high");
    }

    // The launch row: one per port, launch on Enter, the window on Ctrl+Enter,
    // both on the same row so the user reading it sees both.
    const Row* launch = nullptr;
    for (const Row& row : rows) {
        if (row.id.rfind("port:", 0) == 0)
            launch = &row;
    }
    CHECK(launch != nullptr, "the launch row is in the list");
    if (launch) {
        CHECK(launch->action == "launchPort", "the launch row launches its port on Enter",
            launch->action);
        CHECK(launch->title == "tio COM10", "the launch row names its port", launch->title);
        CHECK(launch->moreActions.size() == 1, "the launch row has one more action");
        if (launch->moreActions.size() == 1) {
            CHECK(launch->moreActions[0].first == "openWindow",
                "the extra action opens the window", launch->moreActions[0].first);
            CHECK(launch->moreActions[0].second == "Ctrl+Enter",
                "the extra action is on Ctrl+Enter", launch->moreActions[0].second);
        }
    }
}

// The window's sessions, listed on their own rows: the search box stays honest
// about processes that are out there while the user works elsewhere.
static void testSessionRows()
{
    Lang lang;
    Snapshot s;
    s.port = parsePort("COM10");
    s.serial.baudrate = 115200;

    // No tabs, no session rows: the main list is the plain one.
    CHECK(buildRows(s, Browse::None, lang).size() >= 2, "an empty tab list adds no rows");

    // A snapshot with no enumerated ports still offers the configured one.
    {
        const std::vector<Row> plain = buildRows(s, Browse::None, lang);
        const Row* own = nullptr;
        for (const Row& row : plain) {
            if (row.id == "port:10")
                own = &row;
        }
        CHECK(own != nullptr, "the configured port has its row");
        if (own) {
            CHECK(own->action == "launchPort", "the configured port row launches", own->action);
            CHECK(own->subtitle.find("/dev/ttyS9") != std::string::npos,
                "the row shows the ttyS mapping for its own port", own->subtitle);
        }
    }

    // A running session the window opened, on a port the search box does not
    // name: its own row, stop on Enter, the port in the id.
    SessionView other;
    other.form.port = L"COM8";
    other.form.serial.baudrate = 9600;
    other.form.backend = Backend::Conhost;
    other.session.com = 8;
    other.session.phase = Phase::Running;
    other.session.pid = 4321;
    other.focused = false;
    s.sessions.push_back(other);

    const std::vector<Row> rows = buildRows(s, Browse::None, lang);
    const Row* sessionRow = nullptr;
    for (const Row& row : rows) {
        if (row.id == "stop:8")
            sessionRow = &row;
    }
    CHECK(sessionRow != nullptr, "a running session has its own row");
    if (sessionRow) {
        CHECK(sessionRow->title.find("COM8") != std::string::npos,
            "the session row names its port", sessionRow->title);
        CHECK(sessionRow->action == "stopSession", "the session row stops on Enter",
            sessionRow->action);
        CHECK(sessionRow->subtitle.find("9600") != std::string::npos,
            "the session row shows its own parameters", sessionRow->subtitle);
        CHECK(sessionRow->subtitle.find("running") == std::string::npos,
            "the subtitle is localized, not a phase id", sessionRow->subtitle);
    }

    // The configured port's row is unaffected by another port's session: it
    // still launches.
    for (const Row& row : rows) {
        if (row.id == "port:10")
            CHECK(row.action == "launchPort",
                "another port's session does not block this port's row", row.action);
    }

    // A session bound to the default port IS the launch row: it stops, and no
    // duplicate session row appears for it.
    Snapshot bound = s;
    bound.sessions.clear();
    SessionView own;
    own.form.port = L"COM10";
    own.form.serial.baudrate = 115200;
    own.session.com = 10;
    own.session.phase = Phase::Running;
    own.session.pid = 777;
    own.focused = true;
    bound.sessions.push_back(own);
    const std::vector<Row> boundRows = buildRows(bound, Browse::None, lang);
    const Row* launch = nullptr;
    int stopRows = 0;
    for (const Row& row : boundRows) {
        if (row.id == "port:10")
            launch = &row;
        if (row.action == "stopSession")
            ++stopRows;
    }
    CHECK(launch != nullptr && launch->action == "stopPort",
        "the bound session's stop lives on its port row",
        launch ? launch->action : "");
    CHECK(stopRows == 0, "the bound session is not listed twice");
}

// Two devices, two rows: the search box lists every port it can see, so the
// port is chosen where the launch happens instead of in a hidden setting.
static void testPortRows()
{
    Lang lang;
    Snapshot s;
    s.port = parsePort("COM8");
    s.serial.baudrate = 115200;
    PortInfo eight;
    eight.com = 8;
    eight.instanceId = L"USB\\VID_10C4&PID_EA60\\0001";
    eight.description = L"Silicon Labs CP210x USB to UART Bridge (COM8)";
    PortInfo nine;
    nine.com = 9;
    nine.instanceId = L"USB\\VID_2E8A&PID_4031&MI_02\\8&39BDACDC&0&0002";
    nine.description = L"USB Serial Device (COM9)";
    s.ports = {eight, nine};

    const std::vector<Row> rows = buildRows(s, Browse::None, lang);
    int launchRows = 0;
    const Row* eightRow = nullptr;
    const Row* nineRow = nullptr;
    for (const Row& row : rows) {
        if (row.action != "launchPort")
            continue;
        ++launchRows;
        if (row.id == "port:8")
            eightRow = &row;
        if (row.id == "port:9")
            nineRow = &row;
    }
    CHECK(launchRows == 2, "each port gets one launch row", std::to_string(launchRows));
    CHECK(eightRow != nullptr && nineRow != nullptr, "both ports are listed");
    if (eightRow && nineRow) {
        CHECK(eightRow->title == "tio COM8" && nineRow->title == "tio COM9",
            "a row names its own port");
        CHECK(eightRow->subtitle.find("/dev/ttyS7") != std::string::npos,
            "COM8 maps to ttyS7", eightRow->subtitle);
        CHECK(nineRow->subtitle.find("/dev/ttyS8") != std::string::npos,
            "COM9 maps to ttyS8", nineRow->subtitle);
    }

    // A broken package blocks every row and names the reason; the ports still
    // appear, because another port cannot fix a package.
    Snapshot broken = s;
    broken.defaultReadiness.blocking.push_back(Gate::Package);
    int disabled = 0;
    for (const Row& row : buildRows(broken, Browse::None, lang)) {
        if (row.action != "launchPort" && row.action != "stopPort")
            continue;
        if (row.disabled)
            ++disabled;
    }
    CHECK(disabled == 2, "a broken package blocks every port's row", std::to_string(disabled));
}

// The appearance contract: the look is read from the host's nativeLook, the
// style, the corners and the size, so the window follows all four styles.
static void testAppearance()
{
    // No nativeLook (an old host, or WinShun before its UI is up): the window
    // falls back to fluent's neutrals, and says so.
    {
        Core probe;
        probe.setTheme("light");
        probe.setAppearance(winshun::Json::Object{});
        const Look& look = probe.look();
        CHECK(!look.valid, "no nativeLook means fallback");
        CHECK(!look.dark, "the fallback follows the theme");
        CHECK(look.color("background").empty(), "a fallback colour is absent, not guessed");
        CHECK(look.style == "fluent", "the style defaults to fluent");
    }

    // The notification form has the fields at the top level, initialize's form
    // has them under nativeLook: both are read.
    winshun::Json::Object native {
        {"background", "#ff202020"},
        {"page", "#ff181818"},
        {"text", "#fff3f3f3"},
        {"accent", "#ffa8402c"},
        {"cardRadius", 10.0},
        {"controlRadius", 5.0},
        {"dark", true},
    };
    {
        Core probe;
        probe.setTheme("dark");
        probe.setAppearance(winshun::Json(winshun::Json::Object{{"nativeLook", winshun::Json(native)},
            {"uiStyle", "brand"}, {"uiSize", "roomy"}, {"corners", "round"}, {"transparency", "on"}}));
        const Look& look = probe.look();
        CHECK(look.valid, "initialize's form is read");
        CHECK(look.dark, "nativeLook's dark wins (the style knows)");
        CHECK(look.style == "brand", "the style is read", look.style);
        CHECK(look.uiSize == "roomy", "the size is read");
        CHECK(look.transparency == "on", "the transparency is read");
        CHECK(look.rounded, "corners round means rounded");
        CHECK(look.color("background") == "#ff202020", "a colour is read verbatim",
            look.color("background"));
        CHECK(look.cardRadius == 10.0, "a radius is read");
        CHECK(look.controlRadius == 5.0, "the control radius too");
    }

    // The notification form: the same fields at the top level, and corners
    // square zeroes the radii.
    {
        Core probe;
        probe.setTheme("light");
        probe.setAppearance(winshun::Json(winshun::Json::Object{{"nativeLook", winshun::Json(native)},
            {"corners", "square"}}));
        const Look& look = probe.look();
        CHECK(look.valid, "the notification form is read");
        CHECK(!look.rounded, "corners square is not rounded");
        CHECK(look.cardRadius == 10.0, "the radius is stored, the window zeroes it");
    }

    // An unknown style or size is ignored (the field stays as it was), per the
    // contract: unknown fields are ignored.
    {
        Core probe;
        probe.setTheme("dark");
        probe.setAppearance(winshun::Json(winshun::Json::Object{{"uiStyle", "not-a-style"},
            {"uiSize", "not-a-size"}}));
        CHECK(probe.look().style == "fluent", "an unknown style does not replace the default");
        CHECK(probe.look().uiSize == "normal", "an unknown size does not replace the default");
    }
}

int main()
{
    testPortMapping();
    testPortRejection();
    testParameters();
    testQuoting();
    testBackends();
    testPlans();
    testSession();
    testSessionFailure();
    testReadiness();
    testRows();
    testDiagnosticsView();
    testWindowEntry();
    testSessionRows();
    testPortRows();
    testAppearance();

    if (g_failed == 0) {
        std::printf("PASS %d core checks\n", g_total);
        return 0;
    }
    std::printf("FAILED %d of %d core checks\n", g_failed, g_total);
    return 1;
}
