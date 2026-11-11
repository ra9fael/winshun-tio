// DesignTokens.qml - the window's colours and metrics, from the host's look.
//
// The values come from ThemeBridge (the appearance contract: nativeLook, the
// style, the corners, the size), so the window looks like WinShun in whichever
// of the four styles is set and follows it when the host's appearance changes.
// The names are the window's own; the mapping to the contract's fields is in
// ThemeBridge.cpp.
//
// A QML singleton (QT_QML_SINGLETON_TYPE in CMakeLists.txt), so every component
// reads the same values without threading them through properties.
pragma Singleton
import QtQuick

QtObject {
    // The bridge is a context property qmlmain.cpp installs; a standalone run
    // installs it too, before the window loads.
    readonly property var bridge: typeof themeBridge !== "undefined" ? themeBridge : null

    readonly property bool dark: bridge ? bridge.dark : false
    readonly property string style: bridge ? bridge.style : "fluent"
    readonly property bool rounded: bridge ? bridge.rounded : true

    // Surfaces, from the back to the front.
    readonly property color windowColor: bridge ? bridge.window : "#f7f7f7"
    readonly property color cardColor: bridge ? bridge.card : "#ffffff"
    readonly property color sunkenColor: bridge ? bridge.sunken : "#f0f0f0"
    readonly property color borderColor: bridge ? bridge.border : "#e2e2e2"
    readonly property color hoverColor: bridge ? bridge.hover : "#0a000000"
    readonly property color pressedColor: bridge ? bridge.pressed : "#12000000"

    // Text.
    readonly property color textColor: bridge ? bridge.text : "#1b1b1b"
    readonly property color mutedColor: bridge ? bridge.subtext : "#6b6b6b"
    readonly property color faintColor: bridge ? bridge.faint : "#8a8a8a"

    // Meaning, never alone: every colour here comes with a word.
    readonly property color accentColor: bridge ? bridge.accent : "#0f6cbd"
    readonly property color accentFillColor: bridge ? bridge.accentFill : "#0f6cbd"
    readonly property color accentTextColor: bridge ? bridge.accentText : "#ffffff"
    readonly property color warnColor: bridge ? bridge.warn : "#9d5d00"
    readonly property color dangerColor: bridge ? bridge.danger : "#c42b1c"
    readonly property color okColor: bridge ? bridge.ok : "#0f7b0f"

    // Controls.
    readonly property color controlColor: bridge ? bridge.control : "#ffffff"
    readonly property color controlBorderColor: bridge ? bridge.controlBorder : "#d5d5d5"
    readonly property color inputFocusedColor: bridge ? bridge.inputFocused : "#ffffff"

    // Radii, already 0 when the host's corners are square.
    readonly property double radius: bridge ? bridge.cardRadius : 8
    readonly property double controlRadius: bridge ? bridge.controlRadius : 6
    readonly property double smallRadius: bridge ? bridge.smallRadius : 4

    // Sizes, from the host's 界面大小.
    readonly property int rowHeight: bridge ? bridge.rowHeight : 32
    readonly property int fontBody: bridge ? bridge.fontBody : 16
    readonly property int fontCaption: bridge ? bridge.fontCaption : 14
    readonly property int fontSmall: bridge ? bridge.fontSmall : 11
    readonly property int gap: bridge ? bridge.gap : 12
    readonly property int pad: bridge ? bridge.pad : 16
    readonly property int minWidth: 640
    readonly property int minHeight: 480
}
