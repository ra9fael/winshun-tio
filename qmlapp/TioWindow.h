// TioWindow.h - the standalone window's model.
//
// Path B of the plugin. Every property is read from the same Controller the
// search box uses (core.h), so the two faces cannot disagree: a port picked in
// the window is the one the next `query` answers, and a session started from the
// list shows up here.
//
// The Controller notifies from whichever thread changed it (the protocol thread
// for a launch it was asked to perform), so the hook marshals to the GUI thread
// and the model is only ever touched there.
#pragma once

#include "core.h"
#include "window_hook.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace tioplug {

class TioWindow : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool zh READ zh NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)

    Q_PROPERTY(QString portText READ portText NOTIFY changed)
    Q_PROPERTY(QString deviceText READ deviceText NOTIFY changed)
    Q_PROPERTY(QVariantList ports READ ports NOTIFY changed)
    Q_PROPERTY(QString baudrate READ baudrate NOTIFY changed)
    Q_PROPERTY(QString dataBits READ dataBits NOTIFY changed)
    Q_PROPERTY(QString parity READ parity NOTIFY changed)
    Q_PROPERTY(QString stopBits READ stopBits NOTIFY changed)
    Q_PROPERTY(QString flow READ flow NOTIFY changed)
    Q_PROPERTY(QStringList baudrateOptions READ baudrateOptions CONSTANT)
    Q_PROPERTY(QStringList dataBitsOptions READ dataBitsOptions CONSTANT)
    Q_PROPERTY(QStringList parityOptions READ parityOptions CONSTANT)
    Q_PROPERTY(QStringList stopBitsOptions READ stopBitsOptions CONSTANT)
    Q_PROPERTY(QStringList flowOptions READ flowOptions CONSTANT)

    Q_PROPERTY(QVariantList backends READ backends NOTIFY changed)
    Q_PROPERTY(QString backendText READ backendText NOTIFY changed)

    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString identityTime READ identityTime NOTIFY changed)
    Q_PROPERTY(bool identityValid READ identityValid NOTIFY changed)

    // What the device says about itself, for the details panel: the friendly
    // name, the chip's ids, the serial, the driver and where it is plugged in.
    Q_PROPERTY(QString identityName READ identityName NOTIFY changed)
    Q_PROPERTY(QString identityVendorId READ identityVendorId NOTIFY changed)
    Q_PROPERTY(QString identityProductId READ identityProductId NOTIFY changed)
    Q_PROPERTY(QString identitySerial READ identitySerial NOTIFY changed)
    Q_PROPERTY(QString identityManufacturer READ identityManufacturer NOTIFY changed)
    Q_PROPERTY(QString identityChip READ identityChip NOTIFY changed)
    Q_PROPERTY(QString identityBus READ identityBus NOTIFY changed)
    Q_PROPERTY(QString identityDriver READ identityDriver NOTIFY changed)
    Q_PROPERTY(QString identityLocation READ identityLocation NOTIFY changed)
    Q_PROPERTY(QString identityProblem READ identityProblem NOTIFY changed)

    Q_PROPERTY(QString packageVersion READ packageVersion NOTIFY changed)
    Q_PROPERTY(bool packageValid READ packageValid NOTIFY changed)
    Q_PROPERTY(QString packageProblem READ packageProblem NOTIFY changed)
    // The package's own facts for the integrity card: the upstream it came
    // from, its licence, the runtime file count and their size.
    Q_PROPERTY(QString packageSource READ packageSource NOTIFY changed)
    Q_PROPERTY(QString packageLicense READ packageLicense NOTIFY changed)
    Q_PROPERTY(int packageFileCount READ packageFileCount NOTIFY changed)
    Q_PROPERTY(QString packageSize READ packageSize NOTIFY changed)
    // The third-party record, one entry per component, in the order the
    // manifest lists them.
    Q_PROPERTY(QVariantList thirdParty READ thirdParty NOTIFY changed)

    Q_PROPERTY(bool canLaunch READ canLaunch NOTIFY changed)
    Q_PROPERTY(QString blockedReason READ blockedReason NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY changed)
    Q_PROPERTY(bool sessionBusy READ sessionBusy NOTIFY changed)
    Q_PROPERTY(QString sessionPhase READ sessionPhase NOTIFY changed)
    Q_PROPERTY(QString sessionPortState READ sessionPortState NOTIFY changed)
    Q_PROPERTY(QString sessionPid READ sessionPid NOTIFY changed)
    Q_PROPERTY(QString sessionToken READ sessionToken NOTIFY changed)

    // The session tabs: one entry per tab, in tab order. Each names its port,
    // its friendly name, its phase and whether the window is drawing it.
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)

public:
    explicit TioWindow(QObject* parent = nullptr);
    ~TioWindow() override;

    bool zh() const;
    bool dark() const;
    QString portText() const;
    QString deviceText() const;
    QVariantList ports() const;

    QString baudrate() const;
    QString dataBits() const;
    QString parity() const;
    QString stopBits() const;
    QString flow() const;
    QStringList baudrateOptions() const;
    QStringList dataBitsOptions() const;
    QStringList parityOptions() const;
    QStringList stopBitsOptions() const;
    QStringList flowOptions() const;

    QVariantList backends() const;
    QString backendText() const;

    QString identity() const;
    QString identityTime() const;
    bool identityValid() const;

    QString identityName() const;
    QString identityVendorId() const;
    QString identityProductId() const;
    QString identitySerial() const;
    QString identityManufacturer() const;
    QString identityChip() const;
    QString identityBus() const;
    QString identityDriver() const;
    QString identityLocation() const;
    QString identityProblem() const;

    QString packageVersion() const;
    bool packageValid() const;
    QString packageProblem() const;
    QString packageSource() const;
    QString packageLicense() const;
    int packageFileCount() const;
    QString packageSize() const;
    QVariantList thirdParty() const;

    bool canLaunch() const;
    QString blockedReason() const;
    QString errorText() const;

    bool sessionActive() const;
    bool sessionBusy() const;
    QString sessionPhase() const;
    QString sessionPortState() const;
    QString sessionPid() const;
    QString sessionToken() const;
    QVariantList sessions() const;

    // ---- session tabs ----
    // The window is session-oriented: every tab binds one port to at most one
    // process, and the cards below draw the focused tab.
    Q_INVOKABLE void focusSession(int index);
    // Creates a tab for a port no other tab holds, seeded with the default
    // configuration. Returns false with a notice when the port is taken.
    Q_INVOKABLE bool newSession(int com);
    // Duplicates a tab's parameters into a new tab for the same port; refused
    // while the port is held, since one port carries one process.
    Q_INVOKABLE bool cloneSession(int index);
    // Refused while the tab's session runs: the tab is the only handle to it.
    Q_INVOKABLE bool closeSession(int index);
    Q_INVOKABLE void restart();

    // ---- editing ----
    Q_INVOKABLE void selectPort(int com);
    Q_INVOKABLE void setBaudrate(const QString& rate);
    Q_INVOKABLE void setDataBits(const QString& bits);
    Q_INVOKABLE void setParity(const QString& parity);
    Q_INVOKABLE void setStopBits(const QString& bits);
    Q_INVOKABLE void setFlow(const QString& flow);
    Q_INVOKABLE void setBackend(const QString& id);
    Q_INVOKABLE void refresh();
    // Opens a document that ships in the package folder ("THIRD_PARTY.md",
    // "manifest/files.json"): the integrity card's verifiable links.
    Q_INVOKABLE void openPackageDoc(const QString& name);
    // Opens an upstream http(s) link from the third-party record.
    Q_INVOKABLE void openWebLink(const QString& link);

    // ---- card folding (TIO-008) ----
    Q_INVOKABLE bool cardCollapsed(const QString& id, bool fallback = false) const;
    Q_INVOKABLE void setCardCollapsed(const QString& id, bool collapsed);

    // ---- persistence and the session ----
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool launch();
    Q_INVOKABLE void stop();
    Q_INVOKABLE QString diagnostics() const;
    Q_INVOKABLE void copyDiagnostics();

    // True when the form differs from what is saved: the Save button's enable.
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    bool dirty() const { return m_dirty; }

signals:
    void changed();
    // A message for the window's footer, already localized.
    void notice(const QString& text);

private:
    void reload();
    void recomputeDirty();
    // Runs a form edit: adopts it, marks the form dirty, repaints.
    void edit(const std::function<void(Form&)>& change);
    // The focused tab's form: what the cards below draw.
    Form focusedForm() const;

    Form m_form;          // the window's copy while editing
    bool m_dirty = false;
};

// The one model the window draws, alive for the process.
TioWindow* windowModel();

// Installs the hook plugin.h calls to open the window (window_hook.h).
void installWindowHook(OpenWindowHook hook);

} // namespace tioplug
