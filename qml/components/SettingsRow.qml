import QtQuick
import XMF

// One settings row: label block on the left, controls in a fixed column.
// The label block is clipped to its own column so text can never run under
// the controls at any window width.
Item {
    id: root

    property string title: ""
    property string desc: ""
    property bool showDivider: true
    default property alias controlData: controlRow.data

    implicitHeight: 52
    readonly property int controlX: Math.round(width * 0.55)

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: divider.top

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 16
            width: Math.max(0, root.controlX - 16 - 14)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            clip: true

            Text {
                width: parent.width
                text: root.title
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.desc
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
                visible: root.desc.length > 0
            }
        }

        Row {
            id: controlRow
            anchors.left: parent.left
            anchors.leftMargin: root.controlX
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
        }
    }

    Rectangle {
        id: divider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
        visible: root.showDivider
    }
}
