import QtQuick
import XMF

// Compact page header: title, optional hint line, right-aligned action slot.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string icon: ""
    default property alias actions: slot.data

    implicitHeight: 42
    height: 42

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        IconImage {
            visible: root.icon !== ""
            name: root.icon
            size: 18
            tint: Theme.accent
            anchors.verticalCenter: parent.verticalCenter
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.subtitle !== ""
                text: root.subtitle
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }
    }

    Row {
        id: slot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
    }
}
