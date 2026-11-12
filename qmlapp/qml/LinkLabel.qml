// LinkLabel.qml - a link that looks like one and behaves like one.
//
// The integrity card links two different things: a licence text that ships
// inside the package (opened from disk, the exact copy the launcher
// verified) and an upstream address (opened in the browser). Both are the
// same control here; what they open is the caller's business.
//
// The underline is a drawn rectangle rather than a font underline: the theme
// owns the accent colour, and no font variant is needed for one rule. The
// hover state lives in the MouseArea, so the text itself stays plain.
import QtQuick
import QtQuick.Controls.Basic

Text {
    id: link

    signal clicked()

    color: DesignTokens.accentColor
    font.family: Qt.application.font.family
    font.pixelSize: DesignTokens.fontSmall

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: DesignTokens.accentColor
        opacity: linkArea.containsMouse ? 1 : 0.6
    }

    MouseArea {
        id: linkArea
        anchors.fill: parent
        anchors.margins: -2
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: link.clicked()
    }
}
