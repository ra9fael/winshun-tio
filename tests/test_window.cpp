// test_window.cpp - the standalone window's model, with no window server.
//
// The model (qmlapp/TioWindow.cpp) is what the QML talks to, and it holds the
// rules the search box does not: a form is edited in a copy and only written on
// Save, an outside change must not overwrite what the user is typing, and a
// value the pinned tio build does not accept is refused with a reason.
//
// These run against the real Controller, so a broken rule here is a broken
// window. No Qt widgets and no window are created: only QObject and QVariant.
#include "../qmlapp/TioWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QVariant>

#include <cstdio>
#include <string>

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

namespace {

// The Core is process-wide and the Controller is configured exactly once (the
// same as in the real plugin), so the whole run shares one temporary data
// folder. Each test still keeps its own state apart by saving and reloading.
QString g_dataDir;

struct Sandbox {
    Sandbox()
    {
        // A name of its own per run, so two runs cannot share a folder.
        static int counter = 0;
        g_dataDir = QDir(QDir::tempPath()).filePath(
            QString("tio-model-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter));
        QDir().mkpath(g_dataDir);
    }
    ~Sandbox() { QDir(g_dataDir).removeRecursively(); }
};

// Configures the one Core. Called once, before any model exists.
void configure()
{
    core().setLocale("zh");
    core().setTheme("light");
    // The plugin folder is the one the test binary sits in, so the package
    // check reports what the real plugin would.
    core().controller().configure(QDir::currentPath().toStdWString(),
        g_dataDir.toStdString());
}

// Puts the model back to the saved configuration, the way opening the window
// does.
void reload(TioWindow& model)
{
    core().controller().refresh();
    model.refresh();
}

QVariantMap firstPort(TioWindow& model)
{
    const QVariantList ports = model.ports();
    return ports.isEmpty() ? QVariantMap() : ports.first().toMap();
}

QVariantMap backendOf(TioWindow& model, const char* id)
{
    for (const QVariant& entry : model.backends()) {
        const QVariantMap row = entry.toMap();
        if (row.value("id").toString() == QLatin1String(id))
            return row;
    }
    return QVariantMap();
}

} // namespace

static void testDefaults()
{
    TioWindow model;

    // The option lists are the ones the pinned tio build accepts: closed, so a
    // typo cannot reach argv.
    CHECK(model.baudrateOptions().contains("115200"), "115200 is offered");
    CHECK(model.baudrateOptions().contains("9600"), "9600 is offered");
    CHECK(!model.baudrateOptions().contains("12345"), "an unsupported rate is not offered");
    CHECK(model.dataBitsOptions() == QStringList({"5", "6", "7", "8"}), "the data bits");
    CHECK(model.parityOptions() == QStringList({"none", "even", "odd", "mark", "space"}),
        "the parities");
    CHECK(model.stopBitsOptions() == QStringList({"1", "2"}), "the stop bits");
    CHECK(model.flowOptions() == QStringList({"none", "hard", "soft"}), "the flows");

    // Both backends are listed, and the one this machine lacks says so.
    const QVariantMap wt = backendOf(model, "wt");
    const QVariantMap con = backendOf(model, "conhost");
    CHECK(!wt.isEmpty(), "wt is listed");
    CHECK(!con.isEmpty(), "conhost is listed");
    CHECK(!wt.value("available").toBool() || !wt.value("version").toString().isEmpty(),
        "an available wt names its version");
    // conhost is the one we create ourselves, so it is the owned one.
    CHECK(con.value("owned").toBool(), "conhost is the owned backend");
    CHECK(!wt.value("owned").toBool(), "wt is not owned");
}

static void testEditing()
{
    TioWindow model;
    model.refresh();

    CHECK(!model.dirty(), "a fresh form is not dirty");

    model.setBaudrate("9600");
    CHECK(model.baudrate() == "9600", "the baud rate changed", model.baudrate().toStdString());
    CHECK(model.dirty(), "an edit makes the form dirty");

    model.setDataBits("7");
    CHECK(model.dataBits() == "7", "the data bits changed");
    model.setParity("even");
    CHECK(model.parity() == "even", "the parity changed");
    model.setStopBits("2");
    CHECK(model.stopBits() == "2", "the stop bits changed");
    model.setFlow("hard");
    CHECK(model.flow() == "hard", "the flow changed");

    // A value the pinned build does not take is refused, and the form keeps
    // what it had.
    model.setBaudrate("12345");
    CHECK(model.baudrate() == "9600", "an unsupported baud rate is refused",
        model.baudrate().toStdString());
    model.setDataBits("9");
    CHECK(model.dataBits() == "7", "an unsupported data-bit count is refused",
        model.dataBits().toStdString());
    model.setParity("xor");
    CHECK(model.parity() == "even", "an unsupported parity is refused",
        model.parity().toStdString());
    model.setFlow("xon");
    CHECK(model.flow() == "hard", "an unsupported flow is refused",
        model.flow().toStdString());

    // Saving writes the configuration and clears the dirty flag.
    CHECK(model.save(), "the configuration is saved");
    CHECK(!model.dirty(), "a saved form is not dirty");
}

static void testPortSelection()
{
    TioWindow model;
    model.refresh();

    const QVariantMap port = firstPort(model);
    if (port.isEmpty()) {
        // No serial port on this machine: the selection cannot be exercised
        // here, and nothing else depends on it.
        std::printf("INFO no serial port on this machine: the selection is not checked\n");
        return;
    }
    CHECK(port.value("title").toString().startsWith("COM"), "a port row titles its COMn",
        port.value("title").toString().toStdString());
    CHECK(port.value("device").toString().startsWith("/dev/ttyS"),
        "a port row shows the ttyS mapping", port.value("device").toString().toStdString());
    CHECK(!port.value("identity").toString().isEmpty(), "a port row shows the identity");
    // What the combobox displays is the name Device Manager shows: the friendly
    // name, which names the chip and ends in "(COMn)". It falls back to the
    // bare COMn only when Windows reports no friendly name.
    CHECK(!port.value("name").toString().isEmpty(), "a port row carries the friendly name");
    CHECK(port.value("name").toString().contains(port.value("title").toString()),
        "the friendly name carries its COMn",
        port.value("name").toString().toStdString());

    const int com = port.value("com").toInt();
    model.selectPort(com);
    CHECK(model.portText() == QString("COM%1").arg(com), "the selected port is shown",
        model.portText().toStdString());
    CHECK(model.deviceText() == port.value("device").toString(), "the mapping is shown");
    // Capture is automatic: selecting a port re-reads the identity for that
    // port at once, and the window names the device with the name Windows
    // gives it — never the previous port's identity (TIO-010).
    CHECK(model.identityValid(), "a selection captures the identity");
    CHECK(model.identity() == port.value("identity").toString(),
        "the captured identity is the selected port's",
        model.identity().toStdString());
    // Selecting the port that is already configured is not a change, so the form
    // stays clean; picking a different one is.
    const QVariantList all = model.ports();
    if (all.size() > 1) {
        const QVariantMap otherRow = all.at(1).toMap();
        const int other = otherRow.value("com").toInt();
        model.selectPort(other);
        CHECK(model.portText() == QString("COM%1").arg(other), "another port is selectable",
            model.portText().toStdString());
        CHECK(model.dirty(), "selecting another port makes the form dirty");
        // The identity followed the selection without any further action.
        CHECK(model.identity() == otherRow.value("identity").toString(),
            "picking another port recaptures its identity",
            model.identity().toStdString());
        CHECK(model.identityName() == otherRow.value("name").toString(),
            "the identity names the picked device",
            model.identityName().toStdString());
        model.selectPort(com);
    }

    // A port that is not there is not accepted.
    model.selectPort(0);
    CHECK(model.portText() == QString("COM%1").arg(com), "port 0 is refused",
        model.portText().toStdString());
    model.selectPort(-3);
    CHECK(model.portText() == QString("COM%1").arg(com), "a negative port is refused",
        model.portText().toStdString());
}

// The window is session-oriented: a tab is a place to prepare a session, and it
// may name no port at all. The port is chosen in the card that shows it, and
// only a launch needs one. These checks exercise the lifecycle on the model,
// without launching any process (a real launch is the native probe's business).
static void testSessionTabs()
{
    TioWindow model;
    model.refresh();

    const QVariantList ports = model.ports();
    if (ports.isEmpty()) {
        std::printf("INFO no port on this machine: the tab lifecycle is not checked\n");
        return;
    }

    CHECK(model.sessions().size() >= 1, "the window always has a tab");

    // ＋: a tab with no port, focused, and the form says so.
    CHECK(model.newSession(), "a second tab opens without asking for a port");
    CHECK(model.sessions().size() == 2, "two tabs now");
    CHECK(model.sessions().at(1).toMap().value("focused").toBool(), "the new tab takes the focus");
    CHECK(model.sessions().at(1).toMap().value("com").toInt() == 0, "the new tab names no port");
    CHECK(model.portText() == QString::fromUtf8("—"), "the form shows no port",
        model.portText().toStdString());
    CHECK(!model.canLaunch(), "a tab without a port cannot launch");
    CHECK(!model.blockedReason().isEmpty(), "the gate names why",
        model.blockedReason().toStdString());

    // A port picked in the card is the tab's, and the gate answers for it.
    const int firstCom = ports.at(0).toMap().value("com").toInt();
    model.selectPort(firstCom);
    CHECK(model.portText() == QString("COM%1").arg(firstCom), "the port was picked",
        model.portText().toStdString());
    CHECK(model.sessions().at(1).toMap().value("com").toInt() == firstCom,
        "the tab follows the card");

    // Cloning copies the parameters and drops the port: a clone is another
    // session to prepare, not the same port twice. Two idle tabs may name one
    // port — nothing is exclusive until a session starts.
    CHECK(model.cloneSession(1), "cloning copies the parameters");
    CHECK(model.sessions().size() == 3, "the clone added a tab");
    CHECK(model.sessions().at(2).toMap().value("com").toInt() == 0, "the clone names no port");
    CHECK(model.portText() == QString::fromUtf8("—"), "the clone's form shows no port",
        model.portText().toStdString());

    // Focus moves, the form follows: each tab names its own state.
    model.focusSession(1);
    CHECK(model.portText() == QString("COM%1").arg(firstCom), "the form follows the focused tab",
        model.portText().toStdString());

    // Closing: an idle tab closes; the window is never left without a tab.
    CHECK(model.closeSession(2), "the idle clone closes");
    CHECK(model.sessions().size() == 2, "two tabs remain");
    CHECK(model.closeSession(1), "the other idle tab closes");
    CHECK(model.sessions().size() == 1, "one tab remains");
    CHECK(model.sessions().first().toMap().value("focused").toBool(),
        "the focus fell back to the remaining tab");
}

// Sharing is on by default; both the switch and the port are settings the
// standalone window can change, and the port must survive a reload.
static void testRemoteShareSettings()
{
    TioWindow model;
    model.refresh();

    CHECK(model.shareEnabled(), "serial sharing is enabled by default");
    CHECK(model.sharePort() == 54321, "the default shared port is 54321");

    CHECK(model.setSharePort(54322), "the shared port accepts a valid value");
    CHECK(model.sharePort() == 54322, "the shared port changed");
    CHECK(!model.setSharePort(80), "a privileged port is refused");
    CHECK(model.sharePort() == 54322, "the refused port leaves the value alone");
    CHECK(model.dirty(), "a port edit makes the form dirty");

    CHECK(model.save(), "the sharing settings are saved");
    reload(model);
    CHECK(model.shareEnabled(), "sharing survives a reload");
    CHECK(model.sharePort() == 54322, "the shared port survives a reload");
    CHECK(!model.dirty(), "the saved sharing settings are clean");

    model.setShareEnabled(false);
    CHECK(!model.shareEnabled(), "sharing can be turned off");
    CHECK(model.save(), "turning sharing off is saved");
    reload(model);
    CHECK(!model.shareEnabled(), "sharing stays off after reload");
    model.setSharePort(54321);
    model.setShareEnabled(true);
    model.save();
}

// The shared-content panel: the format toggle, the write gate, and the fact that
// asking to copy or save with nothing received is refused rather than silent.
static void testSharedContentPanel()
{
    TioWindow model;
    model.refresh();

    CHECK(model.shareFormat() == "ascii", "the content view starts in ascii format");
    model.setShareFormat("hex");
    CHECK(model.shareFormat() == "hex", "the view switches to hex");
    model.setShareFormat("bogus");
    CHECK(model.shareFormat() == "ascii", "an unknown format falls back to ascii");

    CHECK(model.shareReadOnly(), "the shared connection starts read-only");
    model.setShareReadOnly(false);
    CHECK(!model.shareReadOnly(), "the write gate can be opened");
    model.setShareReadOnly(true);
    CHECK(model.shareReadOnly(), "the write gate closes again");

    // Logging defaults to on: an unattended serial session is what a log is for.
    CHECK(model.shareLogEnabled(), "auto-logging is on by default");
    CHECK(model.shareLogPath().isEmpty(), "no log file exists before the first byte");
    model.setShareLog(false);
    CHECK(!model.shareLogEnabled(), "auto-logging can be turned off");
    // Turning it off must survive a reload, or the switch looks broken: it would
    // silently come back on with the next refresh.
    reload(model);
    CHECK(!model.shareLogEnabled(), "turning logging off is remembered");
    model.setShareLog(true);
    CHECK(model.shareLogEnabled(), "auto-logging can be turned back on");
    reload(model);
    CHECK(model.shareLogEnabled(), "turning logging on is remembered too");

    CHECK(model.shareLines().isEmpty(), "nothing is drawn before a session runs");
    CHECK(model.shareBytes() == 0, "no bytes are counted before a session runs");
    CHECK(!model.saveShareContent(), "Save as refuses when there is nothing to save");

    // The hex view's shape: three widths, remembered like the log switch.
    CHECK(model.shareHexRow() == 16, "the hex view starts at 16 bytes per row");
    model.setShareHexRow(8);
    CHECK(model.shareHexRow() == 8, "the hex row width can be changed");
    model.setShareHexRow(7);
    CHECK(model.shareHexRow() == 8, "a width the view does not offer is refused");
    reload(model);
    CHECK(model.shareHexRow() == 8, "the hex row width survives a reload");

    CHECK(!model.shareHexAscii(), "the decode column starts off");
    model.setShareHexAscii(true);
    CHECK(model.shareHexAscii(), "the decode column can be turned on");
    reload(model);
    CHECK(model.shareHexAscii(), "the decode column choice survives a reload");
    model.setShareHexAscii(false);

    // Log history: the folder is where the logs live, and nothing is listed
    // before one has been written.
    CHECK(!model.shareLogDir().isEmpty(), "the log folder is known");
    CHECK(model.shareLogDir().endsWith("sessions"), "the log folder is dataDir\\sessions",
        model.shareLogDir().toStdString());
    CHECK(model.shareLogs().isEmpty(), "no logs are listed before anything is written");
}

static void testOutsideChange()
{
    TioWindow model;
    model.refresh();

    // A value different from the saved one, whichever it is: the saved value is
    // whatever the previous test left, and that is the point.
    const QString saved = model.baudrate();
    const QString edit = saved == "9600" ? "19200" : "9600";
    model.setBaudrate(edit);
    CHECK(model.baudrate() == edit, "the form took the edit", model.baudrate().toStdString());
    CHECK(model.dirty(), "the form is dirty");

    // An outside change (a port picked in the search box) must not overwrite
    // what the user is typing: the dirty flag guards the form.
    reload(model);
    CHECK(model.baudrate() == edit, "an outside change does not overwrite the edit",
        model.baudrate().toStdString());
    CHECK(model.dirty(), "the form is still dirty");

    // Saving takes the edit, and the next reload agrees.
    CHECK(model.save(), "the configuration is saved");
    reload(model);
    CHECK(model.baudrate() == edit, "the saved value survives a reload",
        model.baudrate().toStdString());
    CHECK(!model.dirty(), "the saved form is clean");
}

static void testBackendChoice()
{
    TioWindow model;
    model.refresh();

    const QVariantMap wt = backendOf(model, "wt");
    if (wt.value("available").toBool()) {
        model.setBackend("wt");
        CHECK(model.backendText() == "Windows Terminal", "wt was selected",
            model.backendText().toStdString());
    } else {
        // A backend the machine lacks cannot be selected: the form must not
        // promise a terminal that will not start.
        model.setBackend("wt");
        CHECK(model.backendText() != "Windows Terminal",
              "an absent backend is not adopted", model.backendText().toStdString());
    }

    const QVariantMap con = backendOf(model, "conhost");
    if (con.value("available").toBool()) {
        model.setBackend("conhost");
        CHECK(model.backendText().contains("conhost"), "conhost was selected",
            model.backendText().toStdString());
    }
}

static void testLaunchGate()
{
    TioWindow model;
    model.refresh();

    // canLaunch means the six gates pass AND the form on screen is startable.
    // It never depends on a session: stopping is a separate action.
    const bool ready = model.canLaunch();
    if (!ready) {
        CHECK(!model.blockedReason().isEmpty(),
              "a blocked launch names the reason", model.blockedReason().toStdString());
    }
    // The blocked reason names the same gates the search box row does.
    if (!model.canLaunch() && model.blockedReason().isEmpty())
        CHECK(false, "a blocked launch always says why");
}

static void testDiagnostics()
{
    TioWindow model;
    model.refresh();

    const QString text = model.diagnostics();
    CHECK(text.contains("ra9fael.tio"), "the diagnostics name the plugin");
    CHECK(text.contains("integrity"), "the diagnostics report integrity");
    CHECK(text.contains("remote socket clients can write"),
        "the diagnostics warn that remote clients can write");
    CHECK(!text.contains("connected"), "the diagnostics never claim a connection",
        text.toStdString());
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    // One Core for the whole run, configured before any model exists.
    {
        Sandbox box;
        configure();

        testDefaults();
        testEditing();
        testPortSelection();
        testSessionTabs();
        testRemoteShareSettings();
        testSharedContentPanel();
        testOutsideChange();
        testBackendChoice();
        testLaunchGate();
        testDiagnostics();
    }

    if (g_failed == 0) {
        std::printf("PASS %d window model checks\n", g_total);
        return 0;
    }
    std::printf("FAILED %d of %d window model checks\n", g_failed, g_total);
    return 1;
}
