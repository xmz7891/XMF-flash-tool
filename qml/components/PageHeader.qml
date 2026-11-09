import QtQuick
import XMF

// Page header: dark instrument tile, large title, muted subtitle, actions right.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string icon: ""
    default property alias actions: slot.data

    implicitHeight: 66
    height: 66

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        Rectangle {
            visible: root.icon !== ""
            width: 36; height: 36
            radius: Theme.r6
            color: Theme.sectionBg
            anchors.verticalCenter: parent.verticalCenter
            IconImage {
                anchors.centerIn: parent
                name: root.icon
                size: 19
                tint: Theme.accent
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(120, root.width - (root.icon !== "" ? 48 : 0) - slot.width - 24)
            spacing: 2
            Text {
                width: parent.width
                text: root.title
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: root.subtitle !== ""
                text: root.subtitle
                color: Theme.textMuted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
    }

    Row {
        id: slot
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
    }
}
