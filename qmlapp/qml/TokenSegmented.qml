// TokenSegmented.qml - one choice out of a few, on a segmented track.
//
// WinShun's own choice rows (the capture panel's 选区/全屏, the footer's sort)
// put the options side by side on the style's segmented part: one track, one
// pill on the chosen word. The plugin must not import the host's style module
// (CMakeLists: no other plugin's or the host's private module), so this draws
// that shape from the appearance contract: a sunken track, a control-coloured
// pill that slides to the choice, the host's hover and pressed fills, and
// radius zero when the host's corners are square.
//
// The segments share the track in equal parts, so the pill's box is plain
// arithmetic — no measuring of child widths, whose changes a binding would not
// see. The segments are buttons with the radio role: Tab reaches the row, the
// arrow keys move the choice, and screen readers announce it as a radio group —
// the semantics the host's ChoiceButton gives RecordingPanel.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: group

    property var model: []          // the values tio takes, in order
    property string value: ""       // the current one
    signal picked(string choice)

    readonly property int currentIndex: {
        const list = model
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i]) === value)
                return i
        }
        return -1
    }
    // The chosen segment's box, equal shares of the track; 2 inside it on every
    // side, its radius the track's minus 2 — the host's pill geometry.
    readonly property real cell: model.length > 0 ? width / model.length : 0

    // Arrow keys move the choice, as a radio group does; Tab stops at the first
    // segment only, and the arrows carry it from there.
    function move(from: int, delta: int) {
        const next = from + delta
        if (next < 0 || next >= model.length)
            return
        const target = strip.children[next]
        if (target)
            target.forceActiveFocus()
        picked(String(model[next]))
    }

    implicitWidth: 200
    implicitHeight: DesignTokens.rowHeight

    Rectangle { // the track
        anchors.fill: parent
        radius: DesignTokens.controlRadius
        color: DesignTokens.sunkenColor
    }

    Rectangle { // the pill on the chosen segment
        id: pill
        readonly property bool on: group.currentIndex >= 0
                                   && group.currentIndex < group.model.length
        visible: on
        x: on ? group.cell * group.currentIndex + 2 : 0
        y: 2
        width: on ? group.cell - 4 : 0
        height: group.height - 4
        radius: Math.max(0, DesignTokens.controlRadius - 2)
        color: DesignTokens.controlColor
        border.width: 1
        border.color: DesignTokens.controlBorderColor

        Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }

    RowLayout {
        id: strip
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: group.model

            Button {
                id: segment
                required property int index
                required property var modelData
                readonly property bool isCurrent: group.value === String(modelData)

                Layout.fillWidth: true
                Layout.fillHeight: true

                Accessible.role: Accessible.RadioButton
                Accessible.name: String(modelData)
                activeFocusOnTab: index === 0

                Keys.onLeftPressed: group.move(index, -1)
                Keys.onRightPressed: group.move(index, 1)

                background: Rectangle {
                    radius: DesignTokens.smallRadius
                    color: segment.pressed && !segment.isCurrent ? DesignTokens.pressedColor
                         : segment.hovered && !segment.isCurrent ? DesignTokens.hoverColor
                         : "transparent"
                    border.width: segment.visualFocus ? 1 : 0
                    border.color: DesignTokens.accentColor
                }

                contentItem: Text {
                    text: String(segment.modelData)
                    color: !segment.enabled ? DesignTokens.faintColor
                         : segment.isCurrent || segment.hovered ? DesignTokens.textColor
                         : DesignTokens.mutedColor
                    font.pixelSize: DesignTokens.fontCaption
                    font.weight: segment.isCurrent ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: picked(String(modelData))
            }
        }
    }
}
