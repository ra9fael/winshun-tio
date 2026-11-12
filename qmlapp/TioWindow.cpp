// TioWindow.cpp - the standalone window's model (see TioWindow.h).
//
// The window may edit a copy of the form and Save it, which is the one thing the
// search box does not do: there, every change is the whole edit. So this keeps
// `m_form`, seeds it from the Controller, and only writes it back on Save (or on
// Launch, which cannot run a configuration the file does not have). While the
// form is dirty an outside change — a port picked in the search box — does not
// overwrite what the user is typing; the notice says so instead.
#include "TioWindow.h"

#include "logic.h"
#include "native.h"
#include "rows.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QTimer>
#include <QUrl>

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
        || m_form.serial.flow != saved.serial.flow || m_form.backend != saved.backend;
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
        && m_form.backend != Backend::None;
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
            if (info.com == com && !info.description.empty())
                name = fromStd(winshun::utf8(info.description));
        row["name"] = name.isEmpty() ? QString("COM%1").arg(com) : name;
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

bool TioWindow::newSession(int com)
{
    if (com <= 0)
        return false;
    // A new tab starts from the default configuration with the picked port:
    // the picker only offers ports no tab holds, so the controller accepts.
    Form seed = core().controller().form();
    seed.port = L"COM" + std::to_wstring(com);
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

bool TioWindow::launch()
{
    // Launch binds (or reuses) the tab for the port on screen, and saves the
    // parameters first so the search box agrees afterwards. Other tabs'
    // sessions are unaffected: one port carries one process, and this port is
    // this tab's.
    std::string error;
    const bool started = core().controller().launch(m_form, error);
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

void TioWindow::restart()
{
    // Stop waits for the graceful timeout, so the port is free again when the
    // launch follows; the same form restarts on the same port.
    stop();
    launch();
}

QString TioWindow::diagnostics() const
{
    return fromStd(core().controller().diagnostics());
}

void TioWindow::copyDiagnostics()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(diagnostics());
        emit notice(zh() ? "诊断已复制" : "Diagnostics copied");
    }
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
