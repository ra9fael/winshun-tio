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
#include <QGuiApplication>
#include <QTimer>

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
    // A dirty form is the user's work in progress: an outside change must not
    // silently discard it.
    if (!m_dirty)
        m_form = core().controller().form();
}

void TioWindow::recomputeDirty()
{
    const Form saved = core().controller().form();
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
        m_form.backend = core().controller().form().backend;
    }
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

bool TioWindow::canLaunch() const
{
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

bool TioWindow::sessionActive() const { return core().controller().snapshot().session.active(); }
bool TioWindow::sessionBusy() const { return core().controller().snapshot().session.busy(); }

QString TioWindow::sessionPhase() const
{
    return fromStd(phaseId(core().controller().snapshot().session.phase));
}

QString TioWindow::sessionPortState() const
{
    return fromStd(portStateId(core().controller().snapshot().session.port));
}

QString TioWindow::sessionPid() const
{
    const Session session = core().controller().snapshot().session;
    return session.pid ? QString::number(session.pid) : QString();
}

// ---- card folding (TIO-008) -------------------------------------------------------

bool TioWindow::cardCollapsed(const QString& id) const
{
    return core().controller().cardCollapsed(winshun::utf8(id.toStdWString()));
}

void TioWindow::setCardCollapsed(const QString& id, bool collapsed)
{
    core().controller().setCardCollapsed(winshun::utf8(id.toStdWString()), collapsed);
}
QString TioWindow::sessionToken() const
{
    return fromStd(core().controller().snapshot().session.token);
}

// ---- editing ---------------------------------------------------------------------

void TioWindow::selectPort(int com)
{
    if (com <= 0)
        return;
    edit([com](Form& form) { form.port = L"COM" + std::to_wstring(com); });
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
    // Launch cannot run something the file does not have: what is on screen is
    // what starts, and it is saved first so the search box agrees afterwards.
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
    core().controller().stop(false);
    emit notice(zh() ? "已停止本插件启动的会话" : "Stopped the session this plugin started");
    emit changed();
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
