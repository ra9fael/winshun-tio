// ThemeBridge.cpp - the host's look as QML properties (see ThemeBridge.h).
#include "ThemeBridge.h"

#include <QVariant>

namespace tioplug {

namespace {

// The fallback table: what the window drew before it knew about styles, close to
// fluent's neutrals. Used only for a field the host did not send.
QColor fallback(const char* name, bool dark)
{
    struct Entry { const char* name; const char* light; const char* dark; };
    static const Entry table[] = {
        {"background", "#f7f7f7", "#1f1f1f"},
        {"page", "#f3f3f3", "#202020"},
        {"card", "#ffffff", "#2b2b2b"},
        {"sunken", "#f0f0f0", "#232323"},
        {"border", "#e2e2e2", "#3a3a3a"},
        {"hover", "#0a000000", "#0dffffff"},
        {"pressed", "#12000000", "#14ffffff"},
        {"text", "#1b1b1b", "#f2f2f2"},
        {"subtext", "#6b6b6b", "#a0a0a0"},
        {"faint", "#8a8a8a", "#8a8a8a"},
        {"accent", "#0f6cbd", "#4cc2ff"},
        {"accentFill", "#0f6cbd", "#0f6cbd"},
        {"accentText", "#ffffff", "#ffffff"},
        {"control", "#ffffff", "#373737"},
        {"controlBorder", "#d5d5d5", "#444444"},
        {"inputFocused", "#ffffff", "#1f1f1f"},
        {"danger", "#c42b1c", "#ff99a4"},
        {"tagFill", "#0a000000", "#14ffffff"},
    };
    for (const Entry& entry : table) {
        if (std::string(entry.name) == name)
            return QColor(dark ? entry.dark : entry.light);
    }
    return QColor();
}

// nativeLook's colours are #AARRGGBB. A translucent one (the transparency
// setting is on) has to land on an opaque surface before it is drawn there, so
// the caller composites over the window colour.
QColor parse(const std::string& text, QColor fallback)
{
    if (text.empty())
        return fallback;
    const QColor parsed = QString::fromStdString(text);
    return parsed.isValid() ? parsed : fallback;
}

QColor over(const QColor& translucent, const QColor& base)
{
    if (!translucent.isValid() || translucent.alpha() == 255)
        return translucent;
    const qreal a = translucent.alphaF();
    return QColor(
        static_cast<int>(translucent.red() * a + base.red() * (1 - a)),
        static_cast<int>(translucent.green() * a + base.green() * (1 - a)),
        static_cast<int>(translucent.blue() * a + base.blue() * (1 - a)));
}

} // namespace

ThemeBridge* g_bridge = nullptr;

ThemeBridge* themeBridge()
{
    if (!g_bridge)
        g_bridge = new ThemeBridge;
    return g_bridge;
}

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent)
{
    reload();
}

void ThemeBridge::reload()
{
    // The Look is plain data; every binding below reads it through these
    // getters, so one changed() repaints the window.
    emit changed();
}

QString ThemeBridge::style() const { return QString::fromStdString(core().look().style); }
bool ThemeBridge::dark() const { return core().look().dark; }
bool ThemeBridge::rounded() const { return core().look().rounded; }
bool ThemeBridge::native() const { return core().look().valid; }

QColor ThemeBridge::color(const char* name, QColor fallback) const
{
    const Look& look = core().look();
    return parse(look.color(name), fallback);
}

QColor ThemeBridge::window() const
{
    // The window is opaque: a translucent background (transparency on) lands on
    // the page colour, which is what WinShun's own windows do.
    const Look& look = core().look();
    const QColor page = color("page", fallback("page", look.dark));
    return over(color("background", fallback("background", look.dark)), page);
}

QColor ThemeBridge::card() const
{
    const Look& look = core().look();
    return over(color("layer", fallback("card", look.dark)), window());
}

QColor ThemeBridge::sunken() const
{
    // One step towards the window from the card: a well inside it.
    return over(color("hover", fallback("sunken", core().look().dark)), card());
}

QColor ThemeBridge::border() const
{
    return over(color("divider", fallback("border", core().look().dark)), card());
}

QColor ThemeBridge::hover() const
{
    return over(color("hover", fallback("hover", core().look().dark)), card());
}

QColor ThemeBridge::pressed() const
{
    return over(color("pressed", fallback("pressed", core().look().dark)), card());
}

QColor ThemeBridge::text() const { return color("text", fallback("text", core().look().dark)); }
QColor ThemeBridge::subtext() const { return color("subtext", fallback("subtext", core().look().dark)); }
QColor ThemeBridge::faint() const { return color("faint", fallback("faint", core().look().dark)); }

QColor ThemeBridge::accent() const { return color("accent", fallback("accent", core().look().dark)); }
QColor ThemeBridge::accentFill() const
{
    return color("accentFill", fallback("accentFill", core().look().dark));
}
QColor ThemeBridge::accentText() const
{
    return color("accentText", fallback("accentText", core().look().dark));
}

// nativeLook has no caution/success: the window's two status colours keep
// readable fallbacks, and the docs say so.
QColor ThemeBridge::warn() const { return core().look().dark ? QColor("#fce100") : QColor("#9d5d00"); }
QColor ThemeBridge::danger() const { return color("danger", fallback("danger", core().look().dark)); }
QColor ThemeBridge::ok() const { return core().look().dark ? QColor("#6ccb5f") : QColor("#0f7b0f"); }

QColor ThemeBridge::control() const
{
    return over(color("control", fallback("control", core().look().dark)), card());
}
QColor ThemeBridge::controlBorder() const
{
    return over(color("controlBorder", fallback("controlBorder", core().look().dark)), card());
}
QColor ThemeBridge::inputFocused() const
{
    return over(color("inputFocused", fallback("inputFocused", core().look().dark)), card());
}

double ThemeBridge::cardRadius() const
{
    const Look& look = core().look();
    return look.rounded ? look.cardRadius : 0.0;
}
double ThemeBridge::controlRadius() const
{
    const Look& look = core().look();
    return look.rounded ? look.controlRadius : 0.0;
}
double ThemeBridge::smallRadius() const
{
    const Look& look = core().look();
    return look.rounded ? look.smallControlRadius : 0.0;
}

int ThemeBridge::rowHeight() const
{
    // The styles' common row heights, by 界面大小 (UiScale's step).
    const std::string& size = core().look().uiSize;
    return size == "compact" ? 28 : size == "roomy" ? 36 : 32;
}
int ThemeBridge::fontBody() const
{
    const std::string& size = core().look().uiSize;
    return size == "compact" ? 14 : size == "roomy" ? 17 : 16;
}
int ThemeBridge::fontCaption() const
{
    const std::string& size = core().look().uiSize;
    return size == "compact" ? 12 : size == "roomy" ? 15 : 14;
}
int ThemeBridge::fontSmall() const
{
    const std::string& size = core().look().uiSize;
    return size == "compact" ? 10 : size == "roomy" ? 12 : 11;
}
int ThemeBridge::gap() const
{
    const std::string& size = core().look().uiSize;
    return size == "compact" ? 10 : size == "roomy" ? 14 : 12;
}
int ThemeBridge::pad() const
{
    const std::string& size = core().look().uiSize;
    return size == "compact" ? 12 : size == "roomy" ? 20 : 16;
}

} // namespace tioplug
