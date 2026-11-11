// TokenComboBox.qml - a ComboBox drawn from the host's look.
//
// QtQuick.Controls.Basic's default combo is a grey rectangle that matches
// nothing; this one takes its colours and radius from DesignTokens (the
// appearance contract), so it looks like WinShun's own controls in whichever
// style is set. The popup is the same: the host's menu colours, the host's
// menu radius.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ComboBox {
    id: box

    implicitHeight: DesignTokens.rowHeight

    background: Rectangle {
        radius: DesignTokens.controlRadius
        color: box.visualFocus ? DesignTokens.inputFocusedColor : DesignTokens.controlColor
        border.width: 1
        border.color: box.visualFocus ? DesignTokens.accentColor : DesignTokens.controlBorderColor
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: box.indicator.width + box.spacing
        text: box.displayText
        color: DesignTokens.textColor
        font.pixelSize: DesignTokens.fontCaption
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Canvas {
        x: box.width - width - 12
        y: box.height / 2 - height / 2
        width: 10
        height: 6
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = DesignTokens.mutedColor
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(width / 2, height)
            ctx.lineTo(width, 0)
            ctx.stroke()
        }
    }

    delegate: ItemDelegate {
        id: item
        required property var modelData
        width: box.width
        height: DesignTokens.rowHeight
        highlighted: box.highlightedIndex === index

        background: Rectangle {
            radius: DesignTokens.smallRadius
            color: item.highlighted ? DesignTokens.hoverColor : "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                text: typeof modelData === "object" && modelData !== null && "title" in modelData
                      ? modelData.title : modelData
                color: DesignTokens.textColor
                font.pixelSize: DesignTokens.fontCaption
            }
            Label {
                visible: typeof modelData === "object" && modelData !== null
                         && "device" in modelData
                text: typeof modelData === "object" && modelData !== null && "device" in modelData
                      ? modelData.device + " · " + modelData.identity : ""
                color: DesignTokens.mutedColor
                font.pixelSize: DesignTokens.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    popup: Popup {
        y: box.height + 4
        width: box.width
        padding: 4
        implicitHeight: contentItem.implicitHeight + 8

        background: Rectangle {
            radius: DesignTokens.controlRadius
            color: DesignTokens.cardColor
            border.width: 1
            border.color: DesignTokens.borderColor
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            spacing: 2
        }
    }
}
