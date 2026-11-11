// GroupCard.qml - one titled block of rows.
//
// A card folds (TIO-008): clicking anywhere on the header toggles `collapsed`,
// the chevron turns, and the body is skipped from the layout. The window model
// remembers the choice per title, so the fold survives a restart.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card

    property string title: ""
    property string note: ""
    property bool warn: false
    property bool collapsible: true
    property bool collapsed: false
    default property alias content: body.data
    // Extra content in the header, after the note: a card's own actions live
    // here (right-aligned), so a session card's buttons sit in its title row
    // instead of drifting to a button row below.
    property alias headerExtra: headerExtraHost.data

    signal toggled(bool collapsed)

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + DesignTokens.pad * 2
    radius: DesignTokens.radius
    color: DesignTokens.cardColor
    border.width: 1
    border.color: DesignTokens.borderColor

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: DesignTokens.pad
        spacing: DesignTokens.gap

        // The header is wrapped in an Item so the click area can sit over the
        // whole row: a MouseArea as a ColumnLayout sibling would be another
        // row below the title, and a click on the title would never reach it.
        // The MouseArea is declared FIRST, under the content: a header button
        // (headerExtra) must win the click, while the labels — which grab no
        // mouse events — let the fold click fall through to it.
        Item {
            id: headerArea
            Layout.fillWidth: true
            implicitHeight: header.implicitHeight

            MouseArea {
                anchors.fill: parent
                enabled: card.collapsible
                cursorShape: card.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    card.collapsed = !card.collapsed
                    card.toggled(card.collapsed)
                }
            }

            RowLayout {
                id: header
                anchors.fill: parent
                spacing: 8

                // The fold indicator doubles as the affordance: it only appears on
                // cards that can fold, and turns with the state.
                Label {
                    text: card.collapsed ? "\u25B8" : "\u25BE"
                    color: DesignTokens.mutedColor
                    font.pixelSize: DesignTokens.fontSmall
                    visible: card.collapsible
                    Layout.preferredWidth: 12
                }
                Label {
                    text: card.title
                    color: DesignTokens.textColor
                    font.pixelSize: DesignTokens.fontBody
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: card.note
                    color: card.warn ? DesignTokens.warnColor : DesignTokens.mutedColor
                    font.pixelSize: DesignTokens.fontSmall
                    elide: Text.ElideRight
                }
                RowLayout {
                    id: headerExtraHost
                    spacing: 8
                }
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: DesignTokens.gap
            visible: !card.collapsed
        }
    }
}
