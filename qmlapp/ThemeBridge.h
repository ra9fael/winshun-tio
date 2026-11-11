// ThemeBridge.h - the host's look, as QML properties.
//
// The window draws from here instead of its own colour table, so it looks like
// WinShun in whichever of the four styles (fluent / brand / tactile / liquid),
// at whichever size, with corners on or off, and follows the host when any of
// that changes (docs/plugins.md, 外观).
//
// The values come from the appearance contract: nativeLook is the style's own
// colour and radius set, already computed for the current settings. Fields the
// host did not send (an old WinShun, or the moment before its UI is up) fall
// back to the fluent-like table below, which is what the window drew before it
// knew about styles.
#pragma once

#include "core.h"

#include <QColor>
#include <QObject>

namespace tioplug {

class ThemeBridge : public QObject {
    Q_OBJECT

    // Which style is in effect, so the window can say so and adjust the parts a
    // token cannot express.
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(bool rounded READ rounded NOTIFY changed)
    Q_PROPERTY(bool native READ native NOTIFY changed) // nativeLook present at all

    // Surfaces, from the window behind everything to a pressed control.
    Q_PROPERTY(QColor window READ window NOTIFY changed)
    Q_PROPERTY(QColor card READ card NOTIFY changed)
    Q_PROPERTY(QColor sunken READ sunken NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed)

    // Text.
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor subtext READ subtext NOTIFY changed)
    Q_PROPERTY(QColor faint READ faint NOTIFY changed)

    // Meaning, never alone: every colour here comes with a word.
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentFill READ accentFill NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor ok READ ok NOTIFY changed)

    // Controls.
    Q_PROPERTY(QColor control READ control NOTIFY changed)
    Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY changed)
    Q_PROPERTY(QColor inputFocused READ inputFocused NOTIFY changed)

    // Radii, already 0 when the host's corners are square.
    Q_PROPERTY(double cardRadius READ cardRadius NOTIFY changed)
    Q_PROPERTY(double controlRadius READ controlRadius NOTIFY changed)
    Q_PROPERTY(double smallRadius READ smallRadius NOTIFY changed)

    // Sizes, from the host's 界面大小 (compact / normal / roomy).
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY changed)
    Q_PROPERTY(int fontBody READ fontBody NOTIFY changed)
    Q_PROPERTY(int fontCaption READ fontCaption NOTIFY changed)
    Q_PROPERTY(int fontSmall READ fontSmall NOTIFY changed)
    Q_PROPERTY(int gap READ gap NOTIFY changed)
    Q_PROPERTY(int pad READ pad NOTIFY changed)

public:
    explicit ThemeBridge(QObject* parent = nullptr);

    QString style() const;
    bool dark() const;
    bool rounded() const;
    bool native() const;

    QColor window() const;
    QColor card() const;
    QColor sunken() const;
    QColor border() const;
    QColor hover() const;
    QColor pressed() const;
    QColor text() const;
    QColor subtext() const;
    QColor faint() const;
    QColor accent() const;
    QColor accentFill() const;
    QColor accentText() const;
    QColor warn() const;
    QColor danger() const;
    QColor ok() const;
    QColor control() const;
    QColor controlBorder() const;
    QColor inputFocused() const;

    double cardRadius() const;
    double controlRadius() const;
    double smallRadius() const;

    int rowHeight() const;
    int fontBody() const;
    int fontCaption() const;
    int fontSmall() const;
    int gap() const;
    int pad() const;

    // Re-reads the Core's look and repaints what is bound. Called when the host
    // sends an appearance notification and once before the first window.
    void reload();

signals:
    void changed();

private:
    QColor color(const char* name, QColor fallback) const;
};

// The one bridge, alive for the process (the window may be closed and opened
// again, and the appearance notification can arrive while it is closed).
ThemeBridge* themeBridge();

} // namespace tioplug
