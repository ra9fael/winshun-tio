// TokenButton.qml - a Button drawn from the host's look.
//
// Basic's default button is a grey rectangle; this one takes the host's control
// colours and radius, and the highlighted variant uses the host's accent fill
// with the accent text on it — what WinShun's own primary buttons do.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: button

    implicitHeight: DesignTokens.rowHeight
    leftPadding: 14
    rightPadding: 14

    background: Rectangle {
        radius: DesignTokens.controlRadius
        color: !button.enabled ? DesignTokens.sunkenColor
             : button.highlighted ? (button.pressed ? Qt.darker(DesignTokens.accentFillColor, 1.15)
                                                    : button.hovered ? Qt.lighter(DesignTokens.accentFillColor, 1.1)
                                                                     : DesignTokens.accentFillColor)
             : button.pressed ? DesignTokens.pressedColor
             : button.hovered ? DesignTokens.hoverColor
             : DesignTokens.controlColor
        border.width: button.highlighted ? 0 : 1
        border.color: button.enabled ? DesignTokens.controlBorderColor : DesignTokens.borderColor
    }

    contentItem: Text {
        leftPadding: button.leftPadding
        rightPadding: button.rightPadding
        text: button.text
        color: !button.enabled ? DesignTokens.faintColor
             : button.highlighted ? DesignTokens.accentTextColor
             : DesignTokens.textColor
        font.pixelSize: DesignTokens.fontCaption
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
