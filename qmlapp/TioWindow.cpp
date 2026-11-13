// TioWindow.cpp - the standalone window's model (see TioWindow.h).
//
// The window may edit a copy of the form and Save it, which is the one thing the
// search box does not do: there, every change is the whole edit. So this keeps
// `m_form`, seeds it from the Controller, and only writes it back on Save (or on
// Launch, which cannot run a configuration the file does not have). While the
// form is dirty an outside change — a port picked in the search box — does not
// overwrite what the user is typing; the notice says so instead.
#include "TioWindow.h"

#include "ShareClient.h"
#include "AnsiText.h"
#include "logic.h"
#include "native.h"
#include "rows.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <QUrl>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include <cstring>
#include <functional>
#include <mutex>

using namespace Qt::StringLiterals;

namespace tioplug {

namespace {

std::mutex g_hookMutex;

QString fromStd(const std::string& text) { return QString::fromStdString(text); }

QString number(int value) { return QString::number(value); }

// The values the pinned tio build accepts, in the order the cycle actions use.
QStringList rates()
{
    int count = 0;
    const int* values = baudrates(count);
    QStringList out;
    for (int i = 0; i < count; ++i)
        out << QString::number(values[i]);
    return out;
}

int indexOfRate(int rate)
{
    int count = 0;
    const int* values = baudrates(count);
    for (int i = 0; i < count; ++i) {
        if (values[i] == rate)
            return i;
    }
    return -1;
}

} // namespace

TioWindow::TioWindow(QObject* parent) : QObject(parent)
{
    reload();
    // The Controller notifies from whichever thread changed it; the model may
    // only be read on this one.
    core().controller().attach([this] {
        QMetaObject::invokeMethod(this, [this] {
            reload();
            emit changed();
        }, Qt::QueuedConnection);
    });
}

TioWindow::~TioWindow()
{
    if (m_shareClient)
        m_shareClient->stop();
    // The protocol thread may still be serving; dropping the callback is enough
    // (the Controller holds it behind the same mutex it notifies under).
    core().controller().attach(Controller::Notify{});
}

void TioWindow::reload()
{
    // The form mirrors the focused tab: the cards below draw that tab, and a
    // dirty form is the user's work in progress on it, which an outside change
    // must not silently discard.
    if (!m_dirty)
        m_form = focusedForm();
    // Logging is a window preference, not a session one: read it back so the
    // switch shows what the controller persisted (on by default).
    m_shareLogOn = core().controller().shareLog();
    m_shareHexRow = core().controller().shareHexRow();
    m_shareHexAscii = core().controller().shareHexAscii();
    syncShareClient();
}

Form TioWindow::focusedForm() const
{
    // The controller guarantees at least one tab; its focused tab is what the
    // window draws.
    const Snapshot snapshot = core().controller().snapshot();
    for (const SessionView& view : snapshot.sessions)
        if (view.focused)
            return view.form;
    return snapshot.sessions.empty() ? core().controller().form()
                                     : snapshot.sessions.front().form;
}

void TioWindow::recomputeDirty()
{
    // Dirty means "differs from what the settings file records", not "differs
    // from the controller's in-memory form": picking a port previews it in
    // memory without saving, and that must not clear the marker.
    const Form saved = core().controller().savedForm();
    const bool dirty = winshun::utf8(m_form.port) != winshun::utf8(saved.port)
        || m_form.serial.baudrate != saved.serial.baudrate
        || m_form.serial.dataBits != saved.serial.dataBits
        || m_form.serial.parity != saved.serial.parity
        || m_form.serial.stopBits != saved.serial.stopBits
        || m_form.serial.flow != saved.serial.flow || m_form.backend != saved.backend
        || m_form.share != saved.share || m_form.sharePort != saved.sharePort;
    if (dirty != m_dirty) {
        m_dirty = dirty;
        emit changed();
    }
}

void TioWindow::edit(const std::function<void(Form&)>& change)
{
    change(m_form);
    if (m_form.backend == Backend::None) {
        // A backend this machine lacks is not a state the form may hold; keep
        // what was there rather than promising a terminal that will not start.
        m_form.backend = focusedForm().backend;
    }
    // The edit lands on the focused tab, in memory only: a save stays an
    // explicit Save. The controller refuses a port another tab holds.
    std::string error;
    if (!core().controller().preview(m_form, error))
        emit notice(fromStd(describeError(error, zh())));
    recomputeDirty();
    emit changed();
}

// ---- properties ------------------------------------------------------------------

bool TioWindow::zh() const { return core().chinese(); }
bool TioWindow::dark() const { return core().dark(); }

QString TioWindow::portText() const
{
    const Port port = m_form.parsed();
    return port.ok() ? QString("COM%1").arg(port.com) : QString("—");
}

QString TioWindow::deviceText() const
{
    const Port port = m_form.parsed();
    return port.ok() ? fromStd(port.device()) : QString();
}

QVariantList TioWindow::ports() const
{
    QVariantList out;
    const Snapshot snapshot = core().controller().snapshot();
    const Port current = m_form.parsed();
    for (const PortInfo& info : snapshot.ports) {
        QVariantMap row;
        row["com"] = info.com;
        // The name Device Manager shows under Ports (COM & LPT): the friendly
        // name, which ends in "(COMn)". The combobox displays it; a port
        // Windows names only by its device path falls back to the bare COMn.
        const std::wstring name = !info.description.empty()
            ? info.description : L"COM" + std::to_wstring(info.com);
        row["name"] = fromStd(winshun::utf8(name));
        row["title"] = QString("COM%1").arg(info.com);
        row["device"] = fromStd(Port::devicePath(info.com - 1));
        row["identity"] = fromStd(winshun::utf8(info.instanceId));
        row["current"] = current.ok() && current.com == info.com;
        out << row;
    }
    return out;
}

QString TioWindow::baudrate() const { return number(m_form.serial.baudrate); }
QString TioWindow::dataBits() const { return number(m_form.serial.dataBits); }
QString TioWindow::parity() const { return fromStd(parityName(m_form.serial.parity)); }
QString TioWindow::stopBits() const { return number(m_form.serial.stopBits); }
QString TioWindow::flow() const { return fromStd(flowName(m_form.serial.flow)); }

QStringList TioWindow::baudrateOptions() const { return rates(); }
QStringList TioWindow::dataBitsOptions() const { return {"5", "6", "7", "8"}; }
QStringList TioWindow::parityOptions() const { return {"none", "even", "odd", "mark", "space"}; }
QStringList TioWindow::stopBitsOptions() const { return {"1", "2"}; }
QStringList TioWindow::flowOptions() const { return {"none", "hard", "soft"}; }

QVariantList TioWindow::backends() const
{
    QVariantList out;
    const Snapshot snapshot = core().controller().snapshot();
    for (const Backend which : {Backend::Wt, Backend::Conhost}) {
        const bool wt = which == Backend::Wt;
        const bool available = wt ? snapshot.backends.wt : snapshot.backends.conhost;
        QVariantMap row;
        row["id"] = fromStd(backendId(which));
        row["title"] = wt ? QString("Windows Terminal") : QString("conhost");
        row["version"] = wt ? fromStd(snapshot.backends.wtVersion) : QString();
        row["available"] = available;
        row["current"] = m_form.backend == which;
        // Which one owns the process, spelled out: it decides how "Stop" works.
        row["owned"] = !wt;
        out << row;
    }
    return out;
}

QString TioWindow::backendText() const
{
    return fromStd(backendLabel(m_form.backend, makeLang(zh())));
}

QString TioWindow::identity() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid ? fromStd(winshun::utf8(snapshot.identity)) : QString();
}

QString TioWindow::identityTime() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid ? fromStd(winshun::utf8(snapshot.identityTime)) : QString();
}

bool TioWindow::identityValid() const { return core().controller().snapshot().identityValid; }

// The details the device reports about itself. Each field is empty when the
// device or Windows does not say it: the panel shows what exists and nothing
// else.
QString TioWindow::identityName() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid ? fromStd(winshun::utf8(snapshot.identityName)) : QString();
}

QString TioWindow::identityVendorId() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.vendorId)) : QString();
}

QString TioWindow::identityProductId() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.productId)) : QString();
}

QString TioWindow::identitySerial() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.serialNumber)) : QString();
}

QString TioWindow::identityManufacturer() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.manufacturer)) : QString();
}

QString TioWindow::identityChip() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.chip)) : QString();
}

QString TioWindow::identityBus() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.busType)) : QString();
}

QString TioWindow::identityDriver() const
{
    const Snapshot snapshot = core().controller().snapshot();
    const std::wstring& version = snapshot.identityDetails.driverVersion;
    if (!snapshot.identityValid || version.empty())
        return QString();
    return fromStd(winshun::utf8(version));
}

QString TioWindow::identityLocation() const
{
    const Snapshot snapshot = core().controller().snapshot();
    return snapshot.identityValid
        ? fromStd(winshun::utf8(snapshot.identityDetails.location)) : QString();
}

QString TioWindow::identityProblem() const
{
    const Snapshot snapshot = core().controller().snapshot();
    if (!snapshot.identityValid || snapshot.identityDetails.problem.empty())
        return QString();
    return fromStd(winshun::utf8(snapshot.identityDetails.problem));
}

QString TioWindow::packageVersion() const
{
    return fromStd(core().controller().snapshot().packageVersion);
}

bool TioWindow::packageValid() const { return core().controller().snapshot().package.ok; }

QString TioWindow::packageProblem() const
{
    const VerifyResult package = core().controller().snapshot().package;
    if (package.ok || package.problems.empty())
        return QString();
    return fromStd(package.problems.front());
}

QString TioWindow::packageSource() const
{
    return fromStd(core().controller().snapshot().package.sourceUrl);
}

QString TioWindow::packageLicense() const
{
    return fromStd(core().controller().snapshot().package.license);
}

int TioWindow::packageFileCount() const
{
    return core().controller().snapshot().package.fileCount;
}

// The runtime's size, in the unit a reader can judge: bytes below a megabyte,
// megabytes above (one decimal).
QString TioWindow::packageSize() const
{
    const unsigned long long bytes = core().controller().snapshot().package.totalBytes;
    if (bytes < 1024ull * 1024ull)
        return QString::number(bytes / 1024ull) + " KB";
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + " MB";
}

// The third-party record: what each component is, where it came from, which
// licence text ships with it and why it is (or is not) in the package.
QVariantList TioWindow::thirdParty() const
{
    QVariantList out;
    const VerifyResult package = core().controller().snapshot().package;
    for (const ThirdParty& component : package.thirdParty) {
        QVariantMap row;
        row["name"] = fromStd(component.name);
        row["version"] = fromStd(component.version);
        row["license"] = fromStd(component.license);
        row["bundled"] = component.bundled;
        row["sourceLabel"] = fromStd(component.sourceLabel);
        row["source"] = fromStd(component.source);
        row["extraLabel"] = fromStd(component.extraLabel);
        row["extra"] = fromStd(component.extra);
        row["why"] = fromStd(zh() ? component.whyZh : component.whyEn);
        QStringList files;
        for (const std::string& file : component.licenseFiles)
            files << fromStd(file);
        row["licenseFiles"] = files;
        out << row;
    }
    return out;
}

bool TioWindow::canLaunch() const
{
    // The readiness snapshot is the focused tab's: its gates, its Launch button.
    // Other tabs' sessions never appear here — the Session gate is per port.
    return core().controller().snapshot().readiness.ready()
        && validate(m_form.serial) == ParamError::None && m_form.parsed().ok()
        && m_form.backend != Backend::None
        && (!m_form.share || (m_form.sharePort >= 1024 && m_form.sharePort <= 65535));
}

QString TioWindow::blockedReason() const
{
    if (canLaunch())
        return QString();
    const Snapshot snapshot = core().controller().snapshot();
    Readiness readiness = snapshot.readiness;
    // The form, not the saved configuration, decides parameters and backend.
    if (!m_form.parsed().ok() && !readiness.blocking.size())
        readiness.blocking.push_back(Gate::Port);
    if (m_form.share && (m_form.sharePort < 1024 || m_form.sharePort > 65535)
        && std::find(readiness.blocking.begin(), readiness.blocking.end(), Gate::SharePort)
            == readiness.blocking.end())
        readiness.blocking.push_back(Gate::SharePort);
    return fromStd(gateText(readiness, makeLang(zh())));
}

QString TioWindow::errorText() const
{
    // The controller keeps the neutral code; the sentence is localized here, with
    // the same translator the search box uses.
    const std::string code = winshun::utf8(core().controller().error());
    if (code.empty())
        return QString();
    return fromStd(describeError(code, zh()));
}

bool TioWindow::sessionActive() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return view.session.active();
    return false;
}

bool TioWindow::sessionBusy() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return view.session.busy();
    return false;
}

QString TioWindow::sessionPhase() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return fromStd(phaseId(view.session.phase));
    return fromStd(phaseId(Phase::Configuring));
}

QString TioWindow::sessionPortState() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return fromStd(portStateId(view.session.port));
    return fromStd(portStateId(PortState::Unknown));
}

QString TioWindow::sessionPid() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused && view.session.pid)
            return QString::number(view.session.pid);
    return QString();
}

// One entry per tab, in tab order: what the tab bar draws. The name is the
// Device-Manager friendly name of the tab's port, so a tab is recognisable
// before it is clicked.
QVariantList TioWindow::sessions() const
{
    QVariantList out;
    const Snapshot snapshot = core().controller().snapshot();
    for (const SessionView& view : snapshot.sessions) {
        const int com = view.form.parsed().com;
        QVariantMap row;
        row["index"] = static_cast<int>(&view - snapshot.sessions.data());
        row["com"] = com;
        QString name;
        for (const PortInfo& info : snapshot.ports)
            if (com > 0 && info.com == com && !info.description.empty())
                name = fromStd(winshun::utf8(info.description));
        // A tab without a port says nothing above the line that already says
        // "no port": "COM0" would name a port that does not exist.
        row["name"] = com <= 0 ? QString()
                               : (name.isEmpty() ? QString("COM%1").arg(com) : name);
        row["phase"] = fromStd(phaseId(view.session.phase));
        row["running"] = view.session.active();
        row["busy"] = view.session.busy();
        row["focused"] = view.focused;
        out << row;
    }
    return out;
}

void TioWindow::focusSession(int index)
{
    if (index < 0)
        return;
    core().controller().focusSession(static_cast<std::size_t>(index));
    // The form mirrors the focused tab unconditionally: the edits already
    // live in the tab (preview adopts them at once), so nothing is lost by
    // switching, and the cards below must draw the tab the user clicked.
    m_form = focusedForm();
    recomputeDirty();
    emit changed();
}

bool TioWindow::newSession()
{
    // No port: the tab opens where the user can prepare the session, and the
    // port is picked in the card that shows it. Nothing is exclusive until a
    // session starts, so this never fails on a port someone else holds.
    Form seed = core().controller().form();
    seed.port.clear();
    std::string error;
    if (!core().controller().createSession(seed, error)) {
        emit notice(fromStd(describeError(error, zh())));
        return false;
    }
    reload();
    recomputeDirty();
    emit changed();
    return true;
}

bool TioWindow::cloneSession(int index)
{
    std::string error;
    if (!core().controller().cloneSession(static_cast<std::size_t>(index), error)) {
        emit notice(fromStd(describeError(error, zh())));
        return false;
    }
    reload();
    recomputeDirty();
    emit changed();
    return true;
}

bool TioWindow::cloneFocusedSession()
{
    // Which tab is focused, resolved here: a QML loop over tio.sessions is a
    // live view, and the clone it performs changes the list mid-walk.
    const Snapshot snapshot = core().controller().snapshot();
    int focused = -1;
    for (std::size_t i = 0; i < snapshot.sessions.size(); ++i)
        if (snapshot.sessions[i].focused) {
            focused = static_cast<int>(i);
            break;
        }
    if (focused < 0) {
        emit notice(zh() ? "没有可以克隆的会话" : "there is no session to clone");
        return false;
    }
    return cloneSession(focused);
}

bool TioWindow::closeSession(int index)
{
    std::string error;
    if (!core().controller().closeSession(static_cast<std::size_t>(index), error)) {
        emit notice(fromStd(describeError(error, zh())));
        return false;
    }
    reload();
    recomputeDirty();
    emit changed();
    return true;
}

// ---- the session's content --------------------------------------------------------

bool TioWindow::shareEnabled() const { return m_form.share; }
int TioWindow::sharePort() const { return m_form.sharePort; }

bool TioWindow::sessionSharing() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return view.session.active() && view.session.sharing;
    return false;
}

int TioWindow::sessionSharePort() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return view.session.sharePort;
    return 0;
}

bool TioWindow::shareConnected() const
{
    return m_shareClient && m_shareClient->connected();
}

QString TioWindow::shareContent() const
{
    return m_shareContent;
}

// Rebuilds the copyable text from the same data the view draws: the reduced
// terminal screen for ascii, the byte dump for hex. Copy and Save use this, so
// what leaves the window is what the user saw, never a raw escape sequence.
void TioWindow::refreshShareView()
{
    if (!m_shareClient) {
        m_shareContent.clear();
        return;
    }
    if (m_shareFormat == "hex") {
        m_shareContent = fromStd(asHexDump(m_shareClient->tail(128 * 1024), 128 * 1024,
                                           m_shareHexRow, m_shareHexAscii));
        return;
    }
    QString out;
    for (const AnsiLine& line : m_shareClient->lines()) {
        if (!out.isEmpty())
            out += QLatin1Char('\n');
        out += fromStd(line.text());
    }
    m_shareContent = out;
}

// ASCII shows the bytes as text; HEX shows a classic 16-byte dump. Both are
// views of the same received bytes, never of a rewrite of them.
void TioWindow::setShareFormat(const QString& format)
{
    const QString wanted = format == "hex" ? "hex" : "ascii";
    if (wanted == m_shareFormat)
        return;
    m_shareFormat = wanted;
    m_shareRevision = 0;
    refreshShareView();
    emit changed();
}

// Bytes per row in the hex view. Only the three widths the view offers are
// accepted, so a stray value cannot produce a one-byte-per-line dump.
int TioWindow::shareHexRow() const { return m_shareHexRow; }

void TioWindow::setShareHexRow(int bytes)
{
    if (bytes != 8 && bytes != 16 && bytes != 32)
        return;
    if (bytes == m_shareHexRow)
        return;
    m_shareHexRow = bytes;
    core().controller().setShareHexRow(bytes);
    refreshShareView();
    emit changed();
}

// The decode column on the right of the hex view: the same bytes as characters,
// with unprintable ones as '.', which is how a binary frame becomes readable.
bool TioWindow::shareHexAscii() const { return m_shareHexAscii; }

void TioWindow::setShareHexAscii(bool on)
{
    if (on == m_shareHexAscii)
        return;
    m_shareHexAscii = on;
    core().controller().setShareHexAscii(on);
    refreshShareView();
    emit changed();
}

QString TioWindow::shareLogDir() const
{
    return QString::fromStdWString(core().controller().shareLogDir());
}

// The logs already on disk, newest first. The window cannot read the folder
// itself without the controller's dataDir, and a user who is looking for "where
// did my log go" should not have to guess a path.
QVariantList TioWindow::shareLogs() const
{
    QVariantList list;
    const QString dir = shareLogDir();
    if (dir.isEmpty())
        return list;
    const QDir folder(dir);
    const QFileInfoList files = folder.entryInfoList(QStringList{u"share-*.log"_s},
        QDir::Files, QDir::Time);
    for (const QFileInfo& info : files) {
        QVariantMap item;
        item["name"] = info.fileName();
        item["path"] = info.absoluteFilePath();
        item["bytes"] = static_cast<qulonglong>(info.size());
        item["time"] = info.lastModified().toString(u"yyyy-MM-dd HH:mm:ss"_s);
        list << item;
    }
    return list;
}

void TioWindow::openShareLog(const QString& path)
{
    if (path.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void TioWindow::openShareLogDir()
{
    const QString dir = shareLogDir();
    if (dir.isEmpty()) {
        emit notice(zh() ? "还没有日志目录" : "there is no log folder yet");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void TioWindow::sharePoll()
{    if (!m_shareClient)
        return;

    // Incremental: the log writer and the view both consume new bytes exactly
    // once, so a long session does not write the whole tail again every tick.
    const std::string fresh = m_shareClient->consume(m_shareCursor);
    const bool changedNow = !fresh.empty() || m_shareRevision != m_shareClient->revision();
    if (!changedNow)
        return;

    m_shareRevision = m_shareClient->revision();
    m_shareBytes = static_cast<int>(m_shareClient->totalBytes());
    m_shareLogBytes += fresh.size();
    appendShareLog(fresh);

    refreshShareView();
    emit changed();
}

// An optional hit log beside tio's own: the received bytes exactly as they came,
// so the file can be replayed by any tool. It is off until the user turns it on.
void TioWindow::appendShareLog(const std::string& bytes)
{
    if (!m_shareLogOn || bytes.empty())
        return;
    if (m_shareLogPath.isEmpty()) {
        m_shareLogPath = QString::fromStdWString(core().controller().newShareLogPath());
        if (m_shareLogPath.isEmpty())
            return;
    }
    QFile file(m_shareLogPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    file.write(bytes.data(), static_cast<qint64>(bytes.size()));
}

void TioWindow::setShareLog(bool on)
{
    if (on == m_shareLogOn)
        return;
    // Remembered by the controller, so "off" survives a restart instead of
    // silently coming back on with the next launch.
    core().controller().setShareLog(on);
    m_shareLogOn = on;
    if (!on) {
        // Keep the file: turning logging off must not throw away what it holds.
        if (!m_shareLogPath.isEmpty())
            emit notice(zh() ? "已停止写日志：" + QFileInfo(m_shareLogPath).fileName()
                             : "stopped logging to " + QFileInfo(m_shareLogPath).fileName());
        return;
    }
    m_shareLogPath.clear();
    emit notice(zh() ? "开始把共享内容写进 dataDir 的日志"
                     : "logging the shared content into dataDir");
    emit changed();
}

QString TioWindow::shareLogPath() const { return m_shareLogPath; }

// Whether this process may write the clipboard at all.
//
// WinShun runs plugins inside a job object and enforces manifest permissions
// with its UI restrictions: without `clipboard:write` the job carries
// JOB_OBJECT_UILIMIT_WRITECLIPBOARD, and there SetClipboardData is refused while
// OpenClipboard and EmptyClipboard still succeed. Emptying the clipboard bumps
// its sequence number, so "the number moved" is not proof of a copy — it was
// exactly how a blocked copy came to report success. Asking the job first means
// a refused write leaves the user's clipboard untouched.
static bool clipboardWriteAllowed()
{
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    if (::QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions, &ui, sizeof ui,
            nullptr)) {
        if (ui.UIRestrictionsClass & JOB_OBJECT_UILIMIT_WRITECLIPBOARD)
            return false;
    }
    return true;   // not in a job, or the job allows it
}

// Writes CF_UNICODETEXT straight into the clipboard.
//
// WinShun keeps a clipboard history, so the clipboard is contended: it opens the
// clipboard whenever the content changes, and a writer in that instant is
// refused. Qt's QClipboard goes through OLE with delayed rendering, which is
// worse here — until the owner answers a format request the content looks empty
// to every other program, including the history that just triggered. This writes
// the bytes immediately and retries until the clipboard is free. Success is the
// handle SetClipboardData accepted, which is the only thing that actually proves
// the text landed.
static bool writeClipboardText(const QString& text)
{
    if (!clipboardWriteAllowed())
        return false;

    for (int attempt = 0; attempt < 12; ++attempt) {
        if (::OpenClipboard(nullptr)) {
            bool stored = false;
            if (::EmptyClipboard()) {
                const std::size_t bytes
                    = (static_cast<std::size_t>(text.size()) + 1) * sizeof(wchar_t);
                HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (memory) {
                    if (void* target = ::GlobalLock(memory)) {
                        std::memcpy(target, text.utf16(), bytes);
                        ::GlobalUnlock(memory);
                        // A successful SetClipboardData hands the block to
                        // Windows; freeing it here would be a double free.
                        if (::SetClipboardData(CF_UNICODETEXT, memory)) {
                            stored = true;
                            memory = nullptr;
                        }
                    }
                    if (memory)
                        ::GlobalFree(memory);
                }
            }
            ::CloseClipboard();
            if (stored)
                return true;
        }
        // Someone held the clipboard; wait a little longer each time.
        ::Sleep(static_cast<DWORD>(10 + attempt * 15));
    }
    return false;
}

// A refusal that names the real cause: a missing permission needs the user to
// act in WinShun, a busy clipboard only needs another try.
static QString copyFailureText(bool chinese)
{
    if (!clipboardWriteAllowed())
        return chinese
            ? "复制失败：WinShun 没有给本插件剪贴板写入权限。请在 WinShun 里重新启用这个插件并允许「剪贴板」。"
            : "copy failed: WinShun has not granted this plugin clipboard write access. Re-enable the plugin in WinShun and allow Clipboard.";
    return chinese ? "复制失败：剪贴板正被其他程序占用，请稍后重试"
                   : "copy failed: another program is holding the clipboard";
}

// Copies what the view shows (terminal text or the byte dump).
//
// WinShun keeps a clipboard history, so the clipboard is a contended resource:
// it opens the clipboard whenever the content changes, and anything that reads
// or writes it in that instant is refused. Two consequences shaped this:
//
//   * the write is raw Win32, not QClipboard (see writeClipboardText above).
//   * success is the handle SetClipboardData accepted, never the sequence number:
//     under WinShun's job restrictions EmptyClipboard still bumps that number.
void TioWindow::copyShareContent()
{
    const QString text = shareContent();
    if (text.isEmpty()) {
        emit notice(zh() ? "还没有内容可复制" : "there is nothing to copy yet");
        return;
    }
    if (!writeClipboardText(text)) {
        emit notice(copyFailureText(zh()));
        return;
    }
    emit notice(zh() ? "已复制 " + QString::number(text.size()) + " 个字符"
                     : "copied " + QString::number(text.size()) + " characters");
}


// A Save dialog, because the host's Qt has no QuickDialogs (comdlg32 ships with
// Windows and is the dialog every editor on this machine shows).
bool TioWindow::saveShareContent()
{
    const QString text = shareContent();
    if (text.isEmpty()) {
        emit notice(zh() ? "还没有内容可保存" : "there is nothing to save yet");
        return false;
    }

    wchar_t path[MAX_PATH] = L"tio-share.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"Text (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrTitle = zh() ? L"保存共享内容" : L"Save shared content";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn))
        return false;

    const QString target = QString::fromWCharArray(path);
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit notice(zh() ? "写不进这个文件：" + QFileInfo(target).fileName()
                         : "could not write " + QFileInfo(target).fileName());
        return false;
    }
    file.write(text.toUtf8());
    file.close();
    emit notice(zh() ? "已保存为 " + QFileInfo(target).fileName()
                     : "saved as " + QFileInfo(target).fileName());
    return true;
}

// The terminal screen, as rows of styled runs. Colour is a #RRGGBB string, or
// empty for the theme's default; the view maps it straight onto Text colour.
//
// A palette colour (SGR 30..37 / 90..97) is resolved here rather than in the
// reducer, because only the window knows whether the background is light or
// dark: colour 0 is near-black, and near-black on a dark theme is invisible.
// On a dark background the 8 dim entries are swapped for their bright variants.
QVariantList TioWindow::shareLines() const
{
    QVariantList rows;
    if (!m_shareClient)
        return rows;

    const bool onDark = dark();
    const auto resolve = [onDark](int rgb, int index) -> QString {
        if (index < 0)
            return rgb < 0 ? QString()
                           : QString("#%1").arg(rgb, 6, 16, QChar('0'));
        const int slot = index < 8 && onDark ? index + 8 : index;
        return QString("#%1").arg(kAnsiPalette[slot], 6, 16, QChar('0'));
    };

    const std::vector<AnsiLine> screen = m_shareClient->lines();
    for (const AnsiLine& line : screen) {
        QVariantList spans;
        for (const AnsiSpan& span : line.spans) {
            if (span.text.empty())
                continue;
            QVariantMap item;
            item["text"] = fromStd(span.text);
            item["color"] = resolve(span.color, span.colorIndex);
            item["background"] = resolve(span.background, span.backgroundIndex);
            item["bold"] = span.bold;
            item["inverse"] = span.inverse;
            spans << item;
        }
        QVariantMap row;
        row["spans"] = spans;
        rows << row;
    }
    return rows;
}

void TioWindow::clearShareContent()
{
    if (m_shareClient)
        m_shareClient->clear();
    m_shareContent.clear();
    emit changed();
}

void TioWindow::syncShareClient()
{
    const bool sharing = sessionSharing();
    const int port = sessionSharePort();
    if (!sharing) {
        if (m_shareClient) {
            m_shareClient->stop();
            m_shareClient.reset();
        }
        m_shareClientPort = 0;
        m_shareCursor = 0;
        m_shareRevision = 0;
        m_shareBytes = 0;
        if (!m_shareContent.isEmpty()) {
            m_shareContent.clear();
            emit changed();
        }
        return;
    }
    if (m_shareClient && m_shareClient->running() && m_shareClientPort == port)
        return;
    if (!m_shareClient)
        m_shareClient = std::make_unique<ShareClient>();
    if (!m_shareClient->start("127.0.0.1", static_cast<std::uint16_t>(port)))
        m_shareClientPort = 0;
    else
        m_shareClientPort = port;
    // A fresh consumer counts from zero again: keeping the old cursor would put
    // it past the new stream and nothing would ever be read.
    m_shareCursor = 0;
    m_shareRevision = 0;
    m_shareBytes = 0;
    m_shareContent.clear();
    emit changed();
}

void TioWindow::setShareEnabled(bool enabled)
{
    if (sessionActive()) {
        emit notice(zh() ? "请先停止会话，再更改远程共享" : "stop the session before changing remote sharing");
        emit changed();
        return;
    }
    edit([enabled](Form& form) { form.share = enabled; });
    emit changed();
}

void TioWindow::setShareReadOnly(bool readOnly)
{
    m_shareReadOnly = readOnly;
    if (m_shareClient)
        m_shareClient->setReadOnly(readOnly);
    emit notice(readOnly
        ? (zh() ? "共享内容只读：窗口不会向串口写字节" : "read-only: the window writes nothing to the serial device")
        : (zh() ? "已允许窗口向共享串口写入" : "the window may now write to the shared serial device"));
    emit changed();
}

void TioWindow::copySharePort()
{
    // Same contended clipboard as the content: WinShun's history opens it on
    // every change, so this goes through the raw writer too.
    if (!writeClipboardText(QString::number(sharePort()))) {
        emit notice(copyFailureText(zh()));
        return;
    }
    emit notice(zh() ? "Socket 端口已复制" : "Socket port copied");
}

// The port is a setting, so it is validated here rather than at the SpinBox:
// the same rule protects the search box, a hand-edited tio.json and the plan.
bool TioWindow::setSharePort(int port)
{
    if (sessionActive()) {
        emit notice(zh() ? "请先停止会话，再更改共享端口"
                         : "stop the session before changing the shared port");
        emit changed();
        return false;
    }
    if (port < 1024 || port > 65535) {
        emit notice(zh() ? "共享端口范围是 1024 到 65535"
                         : "The shared port must be between 1024 and 65535");
        emit changed();
        return false;
    }
    if (port == m_form.sharePort)
        return true;
    edit([port](Form& form) { form.sharePort = port; });
    // The consumer must follow the configured endpoint. If a session is not
    // running nothing connects yet; the next launch will use this port.
    syncShareClient();
    emit changed();
    return true;
}

void TioWindow::trace(const QString& text)
{
    // A redirected stderr is not always what it claims on this host: a headless
    // run writes to TIO_TRACE_FILE when it is set.
    if (const char* path = std::getenv("TIO_TRACE_FILE")) {
        if (std::FILE* file = std::fopen(path, "ab")) {
            std::fprintf(file, "trace %s\n", qUtf8Printable(text));
            std::fclose(file);
        }
        return;
    }
    std::fprintf(stderr, "tio: trace %s\n", qUtf8Printable(text));
    std::fflush(stderr);
}

// ---- card folding (TIO-008) -------------------------------------------------------

bool TioWindow::cardCollapsed(const QString& id, bool fallback) const
{
    return core().controller().cardCollapsed(winshun::utf8(id.toStdWString()), fallback);
}

void TioWindow::setCardCollapsed(const QString& id, bool collapsed)
{
    core().controller().setCardCollapsed(winshun::utf8(id.toStdWString()), collapsed);
}
QString TioWindow::sessionToken() const
{
    for (const SessionView& view : core().controller().snapshot().sessions)
        if (view.focused)
            return fromStd(view.session.token);
    return QString();
}

// ---- editing ---------------------------------------------------------------------

void TioWindow::selectPort(int com)
{
    if (com <= 0)
        return;
    edit([com](Form& form) { form.port = L"COM" + std::to_wstring(com); });
    // edit() previews into the focused tab, which recaptures the identity for
    // the port just picked (TIO-010); the notice names a refused port.
}

void TioWindow::setBaudrate(const QString& rate)
{
    const int value = rate.toInt();
    if (indexOfRate(value) < 0) {
        emit notice(zh() ? "tio 3.9 不支持这个波特率" : "tio 3.9 does not take that baud rate");
        return;
    }
    edit([value](Form& form) { form.serial.baudrate = value; });
}

void TioWindow::setDataBits(const QString& bits)
{
    const int value = bits.toInt();
    if (value < 5 || value > 8)
        return;
    edit([value](Form& form) { form.serial.dataBits = value; });
}

void TioWindow::setParity(const QString& parity)
{
    Parity value = Parity::None;
    if (!parseParity(parity.toStdString(), value))
        return;
    edit([value](Form& form) { form.serial.parity = value; });
}

void TioWindow::setStopBits(const QString& bits)
{
    const int value = bits.toInt();
    if (value != 1 && value != 2)
        return;
    edit([value](Form& form) { form.serial.stopBits = value; });
}

void TioWindow::setFlow(const QString& flow)
{
    Flow value = Flow::None;
    if (!parseFlow(flow.toStdString(), value))
        return;
    edit([value](Form& form) { form.serial.flow = value; });
}

void TioWindow::setBackend(const QString& id)
{
    const Backend value = backendFromId(id.toStdString());
    if (value == Backend::None)
        return;
    const Snapshot snapshot = core().controller().snapshot();
    const bool available = value == Backend::Wt ? snapshot.backends.wt : snapshot.backends.conhost;
    if (!available) {
        emit notice(zh() ? "这台机器没有这个后端" : "this machine does not have that backend");
        return;
    }
    edit([value](Form& form) { form.backend = value; });
}

void TioWindow::refresh()
{
    core().controller().refresh();
    reload();
    emit changed();
}

// The integrity card's verifiable links: the documents ship in the plugin
// folder, so the file URL opens the exact copy the launcher verified.
void TioWindow::openPackageDoc(const QString& name)
{
    const std::wstring dir = core().controller().pluginDir();
    const QUrl url = QUrl::fromLocalFile(
        QDir::fromNativeSeparators(QString::fromStdWString(dir)) + "/" + name);
    QDesktopServices::openUrl(url);
}

// A web link from the third-party record: the upstream the component came
// from. Only http(s) opens — nothing else is turned into a URL.
void TioWindow::openWebLink(const QString& link)
{
    const QUrl url(link);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https"))
        return;
    QDesktopServices::openUrl(url);
}

// ---- persistence and the session --------------------------------------------------

bool TioWindow::save()
{
    if (!core().controller().save(m_form)) {
        emit notice(zh() ? "配置写不进 dataDir" : "the configuration could not be written");
        return false;
    }
    m_dirty = false;
    emit notice(zh() ? "已保存" : "Saved");
    emit changed();
    return true;
}

bool TioWindow::launch(bool allowRemoteShare)
{
    // Launch binds (or reuses) the tab for the port on screen, and saves the
    // parameters first so the search box agrees afterwards. Other tabs'
    // sessions are unaffected: one port carries one process, and this port is
    // this tab's.
    std::string error;
    const bool started = core().controller().launch(m_form, error, allowRemoteShare);
    if (!started) {
        // The same sentence the footer and the toast show, so the reason is in
        // one place and in the user's language.
        const QString reason = fromStd(describeError(error, zh()));
        emit notice(reason);
        emit changed();
        return false;
    }
    m_dirty = false;
    emit notice(zh() ? "已启动（不代表端口已打开）" : "Started (the port is not necessarily open)");
    emit changed();
    return true;
}

void TioWindow::stop()
{
    // Only the focused tab's session stops: the other tabs keep running, which
    // is the point of a session-oriented window.
    core().controller().stopActive();
    emit notice(zh() ? "已停止这个会话" : "Stopped this session");
    emit changed();
}

void TioWindow::restart(bool allowRemoteShare)
{
    // Stop waits for the graceful timeout, so the port is free again when the
    // launch follows; the same form restarts on the same port.
    const bool share = sessionSharing();
    if (share && !allowRemoteShare) {
        emit notice(zh() ? "重启将重新开放远程串口；请先确认" :
            "Restart will reopen remote serial access; confirm first");
        return;
    }
    stop();
    m_form.share = share && allowRemoteShare;
    launch(share && allowRemoteShare);
}

QString TioWindow::diagnostics() const
{
    return fromStd(core().controller().diagnostics());
}

void TioWindow::copyDiagnostics()
{
    if (!writeClipboardText(diagnostics())) {
        emit notice(copyFailureText(zh()));
        return;
    }
    emit notice(zh() ? "诊断已复制" : "Diagnostics copied");
}

// ---- the process-wide model and the window hook ------------------------------------

namespace {

TioWindow* g_model = nullptr;

// Kept alive for the process: the window may be closed and opened again, and the
// protocol thread keeps reading the same Controller.
TioWindow* model()
{
    if (!g_model)
        g_model = new TioWindow;
    return g_model;
}

} // namespace

TioWindow* windowModel() { return model(); }

void installWindowHook(OpenWindowHook hook)
{
    const std::lock_guard lock(g_hookMutex);
    setOpenWindowHook(std::move(hook));
}

} // namespace tioplug
