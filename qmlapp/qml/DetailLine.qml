// DetailLine.qml - one fact of the device details, labelled.
//
// A fact the device does not report is not shown at all: an empty row would
// read as "the device has no serial", which is a different claim from "the
// device does not say". The value is beside the label, in the muted colour,
// and the ids in the monospaced face so a hex value can be read off.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: line

    property string label: ""
    property string value: ""
    property bool warn: false
    property bool mono: false

    Layout.fillWidth: true
    spacing: 8
    visible: value.length > 0

    Label {
        Layout.preferredWidth: 84
        text: line.label
        color: DesignTokens.mutedColor
        font.family: Qt.application.font.family
        font.pixelSize: DesignTokens.fontSmall
    }

    Label {
        Layout.fillWidth: true
        text: line.value
        color: line.warn ? DesignTokens.warnColor : DesignTokens.textColor
        font.family: line.mono ? "Consolas" : Qt.application.font.family
        font.pixelSize: DesignTokens.fontSmall
        wrapMode: Text.WrapAnywhere
        elide: Text.ElideRight
    }
}
