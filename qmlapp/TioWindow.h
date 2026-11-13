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

#include "ShareClient.h"
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

    // ---- optional bidirectional remote control ----
    Q_PROPERTY(bool shareEnabled READ shareEnabled NOTIFY changed)
    Q_PROPERTY(int sharePort READ sharePort NOTIFY changed)
    Q_PROPERTY(bool sessionSharing READ sessionSharing NOTIFY changed)
    Q_PROPERTY(int sessionSharePort READ sessionSharePort NOTIFY changed)
    // The window's own read-only view of the shared socket: tio mirrors the
    // serial session there, and the GUI connects to loopback to show it.
    Q_PROPERTY(bool shareConnected READ shareConnected NOTIFY changed)
    Q_PROPERTY(QString shareContent READ shareContent NOTIFY changed)
    // "terminal" (ANSI reduced), "ascii" (raw text) or "hex": how the received
    // bytes are rendered. The bytes themselves are never altered.
    Q_PROPERTY(QString shareFormat READ shareFormat WRITE setShareFormat NOTIFY changed)
    Q_PROPERTY(int shareBytes READ shareBytes NOTIFY changed)
    // False means the window may write to the socket (off by default).
    Q_PROPERTY(bool shareReadOnly READ shareReadOnly WRITE setShareReadOnly NOTIFY changed)
    // The terminal screen as rows of styled runs: [{spans:[{text,color,bold}]}]
    Q_PROPERTY(QVariantList shareLines READ shareLines NOTIFY changed)
    // How the hex view draws the same bytes: bytes per row (8/16/32) and whether
    // the decode column is shown. Both are remembered across launches.
    Q_PROPERTY(int shareHexRow READ shareHexRow WRITE setShareHexRow NOTIFY changed)
    Q_PROPERTY(bool shareHexAscii READ shareHexAscii WRITE setShareHexAscii NOTIFY changed)
    // The folder the auto-log writes into; empty until the first byte is logged.
    Q_PROPERTY(QString shareLogDir READ shareLogDir NOTIFY changed)

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

    bool shareEnabled() const;
    int sharePort() const;
    bool sessionSharing() const;
    int sessionSharePort() const;
    bool shareConnected() const;
    QString shareContent() const;
    QString shareFormat() const { return m_shareFormat; }
    void setShareFormat(const QString& format);
    int shareBytes() const { return m_shareBytes; }
    bool shareReadOnly() const { return m_shareReadOnly; }
    void setShareReadOnly(bool readOnly);
    QVariantList shareLines() const;
    int shareHexRow() const;
    void setShareHexRow(int bytes);
    bool shareHexAscii() const;
    void setShareHexAscii(bool on);
    QString shareLogDir() const;
    // The logs already written, newest first: [{name, path, bytes, time}]. Empty
    // when nothing has been logged yet.
    Q_INVOKABLE QVariantList shareLogs() const;
    // Opens one log (or the folder) in whatever the shell uses for it.
    Q_INVOKABLE void openShareLog(const QString& path);
    Q_INVOKABLE void openShareLogDir();

    // ---- session tabs ----
    // The window is session-oriented: every tab binds one port to at most one
    // process, and the cards below draw the focused tab.
    Q_INVOKABLE void focusSession(int index);
    // Creates a tab with no port: a tab is a place to prepare a session, and the
    // port is chosen where it is shown (the Port & parameters card). The launch
    // is what needs a port, and what refuses one already running elsewhere.
    Q_INVOKABLE bool newSession();
    // Duplicates a tab's parameters into a new tab with no port.
    Q_INVOKABLE bool cloneSession(int index);
    // The same for the focused tab. The tab strip calls this, not a QML loop
    // over tio.sessions: that list is a live view of the model, so a clone
    // inside the loop grows the very list being walked and never ends.
    Q_INVOKABLE bool cloneFocusedSession();
    // Refused while the tab's session runs: the tab is the only handle to it.
    Q_INVOKABLE bool closeSession(int index);
    Q_INVOKABLE void restart(bool allowRemoteShare = false);

    // A diagnostic line for a headless run (--auto); stderr, unbuffered.
    Q_INVOKABLE void trace(const QString& text);
    Q_INVOKABLE void setShareEnabled(bool enabled);
    Q_INVOKABLE bool setSharePort(int port);
    Q_INVOKABLE void copySharePort();
    Q_INVOKABLE void clearShareContent();
    // Copies the content exactly as shown (the current format) to the clipboard.
    Q_INVOKABLE void copyShareContent();
    Q_INVOKABLE bool saveShareContent();
    // Opt-in: append every received byte to a log inside dataDir.
    Q_INVOKABLE void setShareLog(bool on);
    Q_PROPERTY(bool shareLogEnabled READ shareLogEnabled NOTIFY changed)
    Q_PROPERTY(QString shareLogPath READ shareLogPath NOTIFY changed)
    bool shareLogEnabled() const { return m_shareLogOn; }
    QString shareLogPath() const;
    // The QML timer calls this to publish the socket's new tail.
    Q_INVOKABLE void sharePoll();

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
    Q_INVOKABLE bool launch(bool allowRemoteShare = false);
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
    // Starts or stops the window's loopback consumer to match the focused
    // session's sharing state; called from reload().
    void syncShareClient();
    // Rebuilds the copyable text (terminal screen or hex dump) from the bytes the
    // consumer holds, so Copy and Save match what the view draws.
    void refreshShareView();
    // Runs a form edit: adopts it, marks the form dirty, repaints.
    void edit(const std::function<void(Form&)>& change);
    // The focused tab's form: what the cards below draw.
    Form focusedForm() const;

    Form m_form;          // the window's copy while editing
    bool m_dirty = false;
    // The window's consumer of tio's shared socket. It is created lazily and
    // stopped whenever the focused session is not sharing.
    std::unique_ptr<ShareClient> m_shareClient;
    int m_shareClientPort = 0;
    // Polling is driven by a timer in the window, so the model only exposes a
    // snapshot rather than waking QML on every socket read.
    QString m_shareContent;
    QString m_shareFormat = "ascii";
    int m_shareBytes = 0;
    bool m_shareReadOnly = true;
    // Absolute byte offset the log writer and the view have consumed.
    std::uint64_t m_shareCursor = 0;
    std::uint64_t m_shareRevision = 0;
    // On by default: reload() reads the persisted value from the controller.
    bool m_shareLogOn = true;
    // The hex view's shape, read back from the controller on reload().
    int m_shareHexRow = 16;
    bool m_shareHexAscii = false;
    QString m_shareLogPath;
    std::uint64_t m_shareLogBytes = 0;
    void appendShareLog(const std::string& bytes);
};

// The one model the window draws, alive for the process.
TioWindow* windowModel();

// Installs the hook plugin.h calls to open the window (window_hook.h).
void installWindowHook(OpenWindowHook hook);

} // namespace tioplug
