// core.h - the one Controller both faces share.
//
// main.cpp serves the protocol (path A, the search box) and qmlapp/ draws the
// standalone window (path B). They must not hold separate state: a parameter
// changed in the window has to be the one the next launch uses, and a session
// started from one face has to be visible in the other.
//
// So the Controller is a process-wide singleton here, configured once with the
// plugin folder and the data folder the host gave at initialize. Everything else
// (rows, QML properties) reads it.
#pragma once

#include "controller.h"
#include "rows.h"
#include "winshun.h"

#include <string>

namespace tioplug {

// The host's look, as the appearance contract spells it out (docs/plugins.md,
// 外观): the style's own colours and radii (nativeLook), plus the settings that
// shape them. A window that draws from here looks like WinShun in whichever of
// the four styles, at whichever size, with corners on or off.
struct Look {
    bool valid = false;          // nativeLook present (an old host may not send it)
    bool dark = false;
    bool rounded = true;         // the 圆角 setting itself
    std::string style = "fluent";   // fluent | brand | tactile | liquid
    std::string uiSize = "normal";  // compact | normal | roomy
    std::string transparency = "off";

    // nativeLook's colours, #AARRGGBB. Empty: the field is absent, use the
    // fallback.
    std::string background, layer, page, divider, text, subtext, faint;
    std::string hover, pressed, control, controlHover, controlPressed, controlBorder;
    std::string inputFocused, accent, accentFill, accentLine, accentText;
    std::string danger, dangerFill, tagFill;

    // nativeLook's radii, logical pixels.
    double cardRadius = 8, controlRadius = 6, largeControlRadius = 8, smallControlRadius = 4;
    double menuRadius = 8, menuItemRadius = 6;

    // A colour by name, or empty when the host did not send it.
    std::string& color(const char* name)
    {
        static std::string none;
        const std::string key = name;
        if (key == "background") return background;
        if (key == "layer") return layer;
        if (key == "page") return page;
        if (key == "divider") return divider;
        if (key == "text") return text;
        if (key == "subtext") return subtext;
        if (key == "faint") return faint;
        if (key == "hover") return hover;
        if (key == "pressed") return pressed;
        if (key == "control") return control;
        if (key == "controlHover") return controlHover;
        if (key == "controlPressed") return controlPressed;
        if (key == "controlBorder") return controlBorder;
        if (key == "inputFocused") return inputFocused;
        if (key == "accent") return accent;
        if (key == "accentFill") return accentFill;
        if (key == "accentLine") return accentLine;
        if (key == "accentText") return accentText;
        if (key == "danger") return danger;
        if (key == "dangerFill") return dangerFill;
        if (key == "tagFill") return tagFill;
        return none;
    }

    const std::string& color(const char* name) const
    {
        return const_cast<Look*>(this)->color(name);
    }
};

class Core {
public:
    // Called once from initialize(). The second call is a no-op, so a window
    // opened before the protocol ran still ends up with the real folders.
    void configure(std::wstring pluginDir, std::string dataDir)
    {
        m_controller.configure(std::move(pluginDir), std::move(dataDir));
    }

    bool configured() const { return m_controller.configured(); }
    Controller& controller() { return m_controller; }
    const Controller& controller() const { return m_controller; }

    // The window's theme and language, kept from the last initialize or
    // appearance message so a window opened later still matches the host.
    void setLocale(std::string locale) { m_locale = std::move(locale); }
    const std::string& locale() const { return m_locale; }
    bool chinese() const { return m_locale != "en"; }

    void setTheme(std::string theme) { m_theme = std::move(theme); }
    const std::string& theme() const { return m_theme; }
    bool dark() const { return m_theme == "dark"; }

    // The host's look. `appearance` is the whole params object of initialize's
    // appearance field or of an appearance notification; unknown fields are
    // ignored, absent ones leave the defaults.
    void setAppearance(const winshun::Json& appearance)
    {
        m_look = readLook(appearance, m_theme == "dark");
    }
    const Look& look() const { return m_look; }

private:
    // Reads one #AARRGGBB or #RRGGBB string, or empty when absent.
    static std::string colorOf(const winshun::Json& object, const char* key)
    {
        const winshun::Json& value = object[key];
        return value.isString() ? value.toString() : std::string();
    }

    static double radiusOf(const winshun::Json& object, const char* key, double fallback)
    {
        return object[key].toNumber(fallback);
    }

    static Look readLook(const winshun::Json& appearance, bool dark)
    {
        Look look;
        look.dark = dark;
        if (!appearance.isObject())
            return look;

        // The whole appearance object when it carries nativeLook; the
        // notification form has the fields at the top level.
        const winshun::Json& native = appearance["nativeLook"].isObject()
            ? appearance["nativeLook"] : appearance;

        look.valid = native.isObject() && native.has("background");
        // Known values only, as WinShun's own SystemTheme::setUiStyle does:
        // an unknown field is ignored, not invented.
        const std::string style = appearance["uiStyle"].toString();
        if (style == "fluent" || style == "brand" || style == "tactile" || style == "liquid")
            look.style = style;
        const std::string size = appearance["uiSize"].toString();
        if (size == "compact" || size == "normal" || size == "roomy")
            look.uiSize = size;
        const std::string transparency = appearance["transparency"].toString();
        if (transparency == "on" || transparency == "off")
            look.transparency = transparency;
        if (appearance["corners"].isString())
            look.rounded = appearance["corners"].toString() != "square";
        if (native["dark"].isBool())
            look.dark = native["dark"].toBool();

        static const char* colors[] = {"background", "layer", "page", "divider", "text",
            "subtext", "faint", "hover", "pressed", "control", "controlHover", "controlPressed",
            "controlBorder", "inputFocused", "accent", "accentFill", "accentLine", "accentText",
            "danger", "dangerFill", "tagFill"};
        for (const char* name : colors)
            look.color(name) = colorOf(native, name);

        look.cardRadius = radiusOf(native, "cardRadius", look.cardRadius);
        look.controlRadius = radiusOf(native, "controlRadius", look.controlRadius);
        look.largeControlRadius = radiusOf(native, "largeControlRadius", look.largeControlRadius);
        look.smallControlRadius = radiusOf(native, "smallControlRadius", look.smallControlRadius);
        look.menuRadius = radiusOf(native, "menuRadius", look.menuRadius);
        look.menuItemRadius = radiusOf(native, "menuItemRadius", look.menuItemRadius);
        return look;
    }

    Controller m_controller;
    std::string m_locale = "zh";
    std::string m_theme = "light";
    Look m_look;
};

// The process-wide instance. Function-local static: no initialization order
// problem between the protocol thread and the GUI thread.
inline Core& core()
{
    static Core instance;
    return instance;
}

// The strings a row or the window needs, in the host's language.
inline Lang makeLang(bool zh)
{
    Lang out;
    out.zh = zh;
    if (zh)
        return out;
    out.groupLaunch = "Launch";
    out.groupSessions = "Sessions (Enter stops)";
    out.groupParameters = "Parameters (Enter edits in place)";
    out.groupInformation = "Information";
    out.stop = "Stop";
    out.openWindow = "Open standalone window";
    out.openWindowNote = "the same configuration, plus identity capture, the session block and Save";
    out.port = "Port";
    out.baudrate = "Baud rate";
    out.dataBits = "Data bits";
    out.parity = "Parity";
    out.stopBits = "Stop bits";
    out.flow = "Flow control";
    out.backend = "Terminal backend";
    out.control = "Control lines DTR / RTS";
    out.controlNote = "defaults not measured · no such CLI option in tio";
    out.diagnostics = "Diagnostics";
    out.diagnosticsNote = "package, hashes, backend, session; never serial content";
    out.noPorts = "(no port found)";
    out.back = "Back";
    out.blocked = "launch blocked";
    out.running = "Running";
    out.starting = "Starting";
    out.stopping = "Stopping";
    out.choice = "Choose";
    out.installed = "installed";
    out.notInstalled = "not installed or not on PATH";
    out.builtIn = "built in · tio is the child process we create";
    out.unavailable = "unavailable";
    out.tioDefault = "tio default";
    return out;
}

// Turns a controller error code into something a user can act on. The codes are
// neutral English ("wt started no tio process"); the sentence is localized here,
// so the search box and the window say the same thing.
inline std::string describeError(const std::string& code, bool zh)
{
    if (code == "a pre-launch check did not pass")
        return zh ? "启动前检查未通过" : "A pre-launch check did not pass";
    if (code == "a session is already active")
        return zh ? "已有会话在运行" : "A session is already running";
    if (code == "the port is held by another session")
        return zh ? "这个端口已被另一个会话占用" : "The port is held by another session";
    if (code == "the session must be stopped before it is closed")
        return zh ? "先停止会话，再关闭这个页签"
                  : "Stop the session before closing its tab";
    if (code == "no session to stop")
        return zh ? "这个端口没有在运行的会话" : "No running session on this port";
    if (code == "no usable backend")
        return zh ? "没有可用后端" : "No usable backend";
    if (code == "the plugin path holds a character Windows Terminal treats as a separator")
        return zh ? "插件路径含有 wt 会当作分隔符的字符（; 或 |）"
                  : "The plugin path holds a character wt treats as a separator (; or |)";
    if (code == "the launch arguments are invalid")
        return zh ? "启动参数无效" : "The launch arguments are invalid";
    if (code == "wt started no tio process (or more than one appeared)")
        return zh ? "wt 没有启动 tio，或同时出现了多个"
                  : "wt started no tio process, or more than one appeared";
    if (code == "the tio process vanished before it could be recorded")
        return zh ? "tio 进程在记录前就消失了" : "The tio process vanished before it could be recorded";
    if (code == "executable not found")
        return zh ? "找不到 runtime\\tio.exe" : "runtime\\tio.exe is missing";
    if (code == "settings could not be written")
        return zh ? "配置写不进 dataDir" : "The configuration could not be written";
    if (code == "settings were not written completely")
        return zh ? "配置没有写完" : "The configuration was not written completely";
    if (code.rfind("access denied", 0) == 0)
        return zh ? "访问被拒绝（1816 表示宿主 Job 不允许子进程）"
                  : "Access denied (1816 means the host job forbids child processes)";
    if (code.rfind("CreateProcess failed", 0) == 0)
        return zh ? "CreateProcess 失败：" + code : code;
    return code.empty() ? (zh ? "启动失败" : "Launch failed") : code;
}

} // namespace tioplug
