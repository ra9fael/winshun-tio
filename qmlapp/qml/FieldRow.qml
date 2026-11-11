// FieldRow.qml - one labelled control in a group.
//
// The label is on the left, the control on the right, and the hint (or the
// reason the control is unavailable) below in the muted colour. Every row shows
// its label as text as well as any colour, so the meaning never depends on the
// colour alone.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: row

    property string label: ""
    property string hint: ""
    property bool warn: false
    default property alias content: slot.data

    Layout.fillWidth: true
    spacing: DesignTokens.gap

    ColumnLayout {
        Layout.preferredWidth: 140
        Layout.alignment: Qt.AlignTop
        spacing: 2

        Label {
            text: row.label
            color: DesignTokens.textColor
            font.pixelSize: DesignTokens.fontCaption
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: row.hint.length > 0
            text: row.hint
            color: row.warn ? DesignTokens.warnColor : DesignTokens.mutedColor
            font.pixelSize: DesignTokens.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    RowLayout {
        id: slot
        Layout.fillWidth: true
        spacing: 8
    }
}
