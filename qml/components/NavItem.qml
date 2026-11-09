import QtQuick
import XMF

// Flat sidebar navigation entry: icon + label, accent selection bar.
Item {
    id: root
    property string text: ""
    property string icon: ""
    property bool active: false
    property bool compact: false
    property int badge: 0
    signal clicked()

    implicitHeight: 32
    height: 32
    width: parent ? parent.width : 160

    Rectangle {
        anchors.fill: parent
        color: root.active ? Theme.selection
                           : (mouse.containsMouse ? Theme.panelHover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        Rectangle {
            width: 3
            height: parent.height
            color: Theme.accent
            visible: root.active
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: root.compact ? 12 : 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        IconImage {
            name: root.icon
            size: 15
            tint: root.active ? Theme.accent : Theme.textSub
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            color: root.active ? Theme.accent : Theme.text
            font.pixelSize: 12
            font.weight: root.active ? Font.DemiBold : Font.Normal
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        visible: root.badge > 0
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(16, badgeText.implicitWidth + 8)
        height: 16
        radius: 8
        color: Theme.accentSoft
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: root.badge
            color: Theme.accent
            font.pixelSize: 9
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
