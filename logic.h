// logic.h - pure rules for the tio launcher plugin (ra9fael.tio).
//
// Nothing here touches Windows, the filesystem, processes or a clock, so
// tests/test_logic.cpp covers it on any C++17 compiler. The Win32 side
// (enumeration, package verification, job/process, the launcher window) lives
// in native.h, ui/ and main.cpp.
//
// Comments are English per the project convention; user-facing text is
// localized in main.cpp. Functions here return stable codes, never display
// text, so a test can assert on the code instead of a translated string.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tioplug {

// ---- COM name -> /dev/ttyS(n-1) (TIO-004) ----------------------------------------
//
// The mapping is a settled convention, not a question: COM1 is ttyS0, COM10 is
// ttyS9, COM256 is ttyS255. Parsing a name never claims the port exists; that
// is a separate, read-only enumeration step.

enum class PortError {
    None,
    Empty,       // nothing typed
    Syntax,      // not COMn at all (a path, a command, trailing junk)
    NotInteger,  // COM1.5
    NotPositive, // COM0, COM-1
    Overflow,    // above COM256: n-1 would leave the ttyS range
};

struct Port {
    PortError error = PortError::Empty;
    int com = 0;      // 1..256 when error == None
    int index = -1;   // com - 1, the ttyS number

    bool ok() const { return error == PortError::None; }
    // "/dev/ttyS9" when ok(), "" otherwise.
    std::string device() const { return ok() ? devicePath(index) : std::string(); }

    static std::string devicePath(int index)
    {
        return "/dev/ttyS" + std::to_string(index);
    }
};

// Accepts "COM10", "com10", "Com 10", " COM10 ". Rejects everything else,
// including signs, fractions, hex and anything after the digits.
inline Port parsePort(const std::string& raw)
{
    Port result;
    // Trim ASCII whitespace only: a name is ASCII by definition.
    std::size_t begin = 0;
    std::size_t end = raw.size();
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (begin < end && space(raw[begin]))
        ++begin;
    while (end > begin && space(raw[end - 1]))
        --end;
    if (begin == end) {
        result.error = PortError::Empty;
        return result;
    }

    std::size_t at = begin;
    if (end - at < 4) { // "com" plus at least one digit
        result.error = PortError::Syntax;
        return result;
    }
    const auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    if (lower(raw[at]) != 'c' || lower(raw[at + 1]) != 'o' || lower(raw[at + 2]) != 'm') {
        result.error = PortError::Syntax;
        return result;
    }
    at += 3;
    while (at < end && (raw[at] == ' ' || raw[at] == '\t'))
        ++at;
    if (at == end) {
        result.error = PortError::Syntax;
        return result;
    }
    if (raw[at] == '+' || raw[at] == '-') {
        // A sign is not part of a port name; treat as syntax, not as a number
        // that happens to be negative, so the message points at the real fault.
        result.error = PortError::Syntax;
        return result;
    }

    long long value = 0;
    bool digits = false;
    for (; at < end; ++at) {
        const char c = raw[at];
        if (c < '0' || c > '9') {
            // A dot, a letter or trailing junk: refuse rather than truncate.
            result.error = PortError::NotInteger;
            return result;
        }
        digits = true;
        // Saturate well past 256 so a very long digit run cannot overflow.
        if (value < 1000000)
            value = value * 10 + (c - '0');
    }
    if (!digits) {
        result.error = PortError::NotInteger;
        return result;
    }
    if (value <= 0) {
        result.error = PortError::NotPositive;
        return result;
    }
    if (value > 256) {
        result.error = PortError::Overflow;
        return result;
    }
    result.error = PortError::None;
    result.com = static_cast<int>(value);
    result.index = result.com - 1;
    return result;
}

// ---- serial parameters (TIO-011) -------------------------------------------------
//
// Only values the pinned tio build accepts: -b, -d 5..8, -p, -s 1|2, -f.
// Anything else is rejected before a process exists.

enum class Parity { None, Even, Odd, Mark, Space };
enum class Flow { None, Hard, Soft };

struct Serial {
    int baudrate = 115200;
    int dataBits = 8;
    Parity parity = Parity::None;
    int stopBits = 1;
    Flow flow = Flow::None;
};

inline const char* parityName(Parity p)
{
    switch (p) {
    case Parity::Even: return "even";
    case Parity::Odd: return "odd";
    case Parity::Mark: return "mark";
    case Parity::Space: return "space";
    case Parity::None: break;
    }
    return "none";
}

inline const char* flowName(Flow f)
{
    switch (f) {
    case Flow::Hard: return "hard";
    case Flow::Soft: return "soft";
    case Flow::None: break;
    }
    return "none";
}

enum class ParamError {
    None,
    Baudrate, // not a positive rate the build supports
    DataBits, // outside 5..8
    Parity,   // not one of the five names
    StopBits, // not 1 or 2
    Flow,     // not none/hard/soft
};

// One error at a time keeps the form's message unambiguous; the caller calls
// this again after the field it named is corrected.
inline ParamError validate(const Serial& s)
{
    if (s.baudrate <= 0)
        return ParamError::Baudrate;
    if (s.dataBits < 5 || s.dataBits > 8)
        return ParamError::DataBits;
    if (s.stopBits != 1 && s.stopBits != 2)
        return ParamError::StopBits;
    return ParamError::None;
}

inline bool parseParity(const std::string& text, Parity& out)
{
    if (text == "none") { out = Parity::None; return true; }
    if (text == "even") { out = Parity::Even; return true; }
    if (text == "odd") { out = Parity::Odd; return true; }
    if (text == "mark") { out = Parity::Mark; return true; }
    if (text == "space") { out = Parity::Space; return true; }
    return false;
}

inline bool parseFlow(const std::string& text, Flow& out)
{
    if (text == "none") { out = Flow::None; return true; }
    if (text == "hard") { out = Flow::Hard; return true; }
    if (text == "soft") { out = Flow::Soft; return true; }
    return false;
}

// ---- argv quoting ---------------------------------------------------------------
//
// CreateProcessW takes one command line, so every argument is quoted by the
// rules CommandLineToArgvW and the CRT use: wrap in quotes when the text is
// empty or holds whitespace or a quote, and double every backslash that sits
// right before a quote (or before the closing quote).
//
// The text is UTF-8 and is handled byte by byte. That is exact rather than
// approximate: quotes, backslashes, spaces and tabs are all ASCII, and no byte
// of a multi-byte UTF-8 sequence is below 0x80, so a space inside CJK text can
// never be mistaken for a separator.

inline std::string quoteArg(const std::string& text)
{
    const bool needsQuotes = text.empty()
        || text.find_first_of(" \t\n\v\"") != std::string::npos;
    if (!needsQuotes)
        return text;

    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    std::size_t backslashes = 0;
    for (const char c : text) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            // 2n+1 backslashes then a quote: n literal backslashes, one escaped quote.
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(c);
    }
    // Trailing backslashes precede the closing quote and must be doubled.
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

// Joins already-unquoted arguments into one command line.
inline std::string buildCommandLine(const std::vector<std::string>& argv)
{
    std::string line;
    for (const std::string& arg : argv) {
        if (!line.empty())
            line.push_back(' ');
        line += quoteArg(arg);
    }
    return line;
}

// ---- backend choice (TIO-005) ---------------------------------------------------
//
// Windows Terminal is preferred; conhost is the fallback. The choice is saved
// with the rest of the configuration, so a machine without wt.exe keeps using
// conhost without asking again.

enum class Backend { None, Wt, Conhost };

struct Backends {
    bool wt = false;
    bool conhost = false;
    std::string wtVersion; // empty when wt was not found
};

inline Backend preferredBackend(const Backends& found)
{
    if (found.wt)
        return Backend::Wt;
    if (found.conhost)
        return Backend::Conhost;
    return Backend::None;
}

inline bool available(const Backends& found, Backend backend)
{
    switch (backend) {
    case Backend::Wt: return found.wt;
    case Backend::Conhost: return found.conhost;
    case Backend::None: break;
    }
    return false;
}

// The saved choice stands while its backend is still there; otherwise fall back
// to the preference order. `changed` says whether the caller should persist the
// new value.
inline Backend resolveBackend(const Backends& found, Backend saved, bool* changed = nullptr)
{
    const bool keep = available(found, saved);
    if (changed)
        *changed = !keep;
    return keep ? saved : preferredBackend(found);
}

inline const char* backendId(Backend backend)
{
    switch (backend) {
    case Backend::Wt: return "wt";
    case Backend::Conhost: return "conhost";
    case Backend::None: break;
    }
    return "none";
}

inline Backend backendFromId(const std::string& id)
{
    if (id == "wt")
        return Backend::Wt;
    if (id == "conhost")
        return Backend::Conhost;
    return Backend::None;
}

// Windows Terminal splits its command line on ';' and '|' before it parses
// quotes, so a package path holding either character cannot be passed as one
// command. Refusing is the only honest option: rewriting the path would point
// the session at a different file.
inline bool pathFitsWt(const std::string& path)
{
    return path.find_first_of(";|") == std::string::npos;
}

// ---- launch plans ---------------------------------------------------------------

enum class PlanError {
    None,
    Port,
    Parameters,
    PackagePath, // holds ';' or '|', which wt cannot take
};

struct Plan {
    PlanError error = PlanError::None;
    Backend backend = Backend::None;
    std::vector<std::string> tio; // tio.exe and its arguments, unquoted
    std::vector<std::string> program; // what CreateProcessW runs, unquoted
    std::string title; // wt tab title, carries the session token
};

// tio's own arguments. `--no-reconnect` is deliberate: the launcher owns
// reconnect policy (TIO-010), and tio's automatic reconnect would silently
// follow a reused COM number.
inline std::vector<std::string> tioArguments(const Port& port, const Serial& serial)
{
    return {
        "-n",
        "-b", std::to_string(serial.baudrate),
        "-d", std::to_string(serial.dataBits),
        "-p", parityName(serial.parity),
        "-s", std::to_string(serial.stopBits),
        "-f", flowName(serial.flow),
        port.ok() ? port.device() : std::string(),
    };
}

// `wtBin` and `tioExe` are absolute paths. `token` makes the tab title unique
// per session, which is how a session is found again after wt.exe has handed
// the process to the resident Windows Terminal.
inline Plan planLaunch(Backend backend, const std::string& wtBin, const std::string& tioExe,
                       const Port& port, const Serial& serial, const std::string& token)
{
    Plan plan;
    plan.backend = backend;
    if (!port.ok()) {
        plan.error = PlanError::Port;
        return plan;
    }
    if (validate(serial) != ParamError::None) {
        plan.error = PlanError::Parameters;
        return plan;
    }
    if (backend == Backend::None) {
        plan.error = PlanError::Port;
        return plan;
    }

    plan.tio.push_back(tioExe);
    for (const std::string& arg : tioArguments(port, serial))
        plan.tio.push_back(arg);

    if (backend == Backend::Conhost) {
        plan.program = plan.tio;
        return plan;
    }

    if (!pathFitsWt(wtBin) || !pathFitsWt(tioExe)) {
        plan.error = PlanError::PackagePath;
        return plan;
    }

    plan.title = "tio COM" + std::to_string(port.com) + " " + token;
    plan.program.push_back(wtBin);
    plan.program.push_back("-w");
    plan.program.push_back("new");
    plan.program.push_back("--title");
    plan.program.push_back(plan.title);
    // Keep tio from renaming the tab and losing the token.
    plan.program.push_back("--suppressApplicationTitle");
    plan.program.insert(plan.program.end(), plan.tio.begin(), plan.tio.end());
    return plan;
}

// ---- session state (TIO-007/013) -------------------------------------------------
//
// A created process means started, never connected: tio prints "Connected" in
// its own terminal and the launcher cannot read it. So the port state is its own
// value, and it stays Unknown unless the pinned build gives a verifiable signal.

enum class Phase {
    Configuring,
    Validating,
    Starting,
    Running,
    Stopping,
    Exited,
    Failed,
    Removed,
};

enum class PortState {
    Unknown,   // started, and nothing proves the port is open
    Open,      // only with evidence from this build
    Busy,      // the upstream reported the port is taken
    Denied,    // the upstream reported access denied
    Gone,      // the device left
};

inline const char* phaseId(Phase phase)
{
    switch (phase) {
    case Phase::Configuring: return "configuring";
    case Phase::Validating: return "validating";
    case Phase::Starting: return "starting";
    case Phase::Running: return "running";
    case Phase::Stopping: return "stopping";
    case Phase::Exited: return "exited";
    case Phase::Failed: return "failed";
    case Phase::Removed: return "removed";
    }
    return "configuring";
}

inline const char* portStateId(PortState state)
{
    switch (state) {
    case PortState::Unknown: return "unknown";
    case PortState::Open: return "open";
    case PortState::Busy: return "busy";
    case PortState::Denied: return "denied";
    case PortState::Gone: return "gone";
    }
    return "unknown";
}

struct Session {
    Phase phase = Phase::Configuring;
    PortState port = PortState::Unknown;
    int com = 0;
    std::uint32_t pid = 0;         // owned process, 0 before start
    std::uint64_t created = 0;     // process creation time, guards PID reuse
    std::string token;             // per-session id, also in the wt title
    std::string device;

    bool active() const
    {
        return phase == Phase::Starting || phase == Phase::Running || phase == Phase::Stopping;
    }
    bool busy() const { return phase == Phase::Validating || phase == Phase::Starting || phase == Phase::Stopping; }
};

// Rejects a transition that would run two sessions at once or resurrect a dead
// one. Returns false and leaves the session untouched.
inline bool beginValidation(Session& s)
{
    if (s.active() || s.phase == Phase::Validating)
        return false;
    s.phase = Phase::Validating;
    s.port = PortState::Unknown;
    return true;
}

inline bool beginStart(Session& s)
{
    if (s.phase != Phase::Validating)
        return false;
    s.phase = Phase::Starting;
    return true;
}

inline bool markRunning(Session& s, std::uint32_t pid, std::uint64_t created)
{
    if (s.phase != Phase::Starting || pid == 0)
        return false;
    s.phase = Phase::Running;
    s.pid = pid;
    s.created = created;
    s.port = PortState::Unknown;
    return true;
}

// A launch that never produced a process, or one that died at once.
inline bool markFailed(Session& s, PortState port)
{
    if (s.phase == Phase::Exited || s.phase == Phase::Failed || s.phase == Phase::Removed)
        return false;
    s.phase = Phase::Failed;
    s.port = port;
    return true;
}

inline bool beginStop(Session& s)
{
    if (!s.active() || s.phase == Phase::Stopping)
        return false;
    s.phase = Phase::Stopping;
    return true;
}

inline bool markExited(Session& s)
{
    if (!s.active())
        return false;
    s.phase = Phase::Exited;
    s.pid = 0;
    s.created = 0;
    return true;
}

// The device left: the session is void, and the same COM number must not be
// picked up again automatically (TIO-010).
inline bool markRemoved(Session& s)
{
    if (s.phase == Phase::Exited || s.phase == Phase::Removed)
        return false;
    s.phase = Phase::Removed;
    s.port = PortState::Gone;
    return true;
}

// A PID is only ours while its creation time matches: after a restart the same
// number belongs to another process (TIO-007).
inline bool ownedProcess(const Session& s, std::uint32_t pid, std::uint64_t created)
{
    return s.pid != 0 && s.pid == pid && s.created != 0 && s.created == created;
}

// ---- activation -----------------------------------------------------------------
//
// The launch button is enabled only when every gate passes. `reasons` names the
// failing gates in a stable order, so the form and the tests agree on what is
// missing and in which order it is reported.

enum class Gate {
    Port,       // the name parses and maps
    Parameters, // the serial values are in range
    Identity,   // the device snapshot still matches
    Package,    // every packaged file hashes as recorded
    Backend,    // the chosen terminal is still there
    Session,    // no session is already active
};

struct Readiness {
    std::vector<Gate> blocking;
    bool ready() const { return blocking.empty(); }
};

struct Environment {
    Port port;
    Serial serial;
    bool identityKnown = true;
    bool packageValid = true;
    Backend backend = Backend::None;
    bool sessionActive = false;
};

inline Readiness check(const Environment& env)
{
    Readiness readiness;
    if (!env.port.ok())
        readiness.blocking.push_back(Gate::Port);
    if (validate(env.serial) != ParamError::None)
        readiness.blocking.push_back(Gate::Parameters);
    if (!env.identityKnown)
        readiness.blocking.push_back(Gate::Identity);
    if (!env.packageValid)
        readiness.blocking.push_back(Gate::Package);
    if (env.backend == Backend::None)
        readiness.blocking.push_back(Gate::Backend);
    if (env.sessionActive)
        readiness.blocking.push_back(Gate::Session);
    return readiness;
}

inline const char* gateId(Gate gate)
{
    switch (gate) {
    case Gate::Port: return "port";
    case Gate::Parameters: return "parameters";
    case Gate::Identity: return "identity";
    case Gate::Package: return "package";
    case Gate::Backend: return "backend";
    case Gate::Session: return "session";
    }
    return "port";
}

} // namespace tioplug
