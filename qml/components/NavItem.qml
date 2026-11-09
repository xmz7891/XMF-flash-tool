import QtQuick
import XMF

// Sidebar entry: amber selection bar, dark text, 34px rows.
Item {
    id: root
    property string text: ""
    property string icon: ""
    property bool active: false
    property bool compact: false
    property int badge: 0
    signal clicked()

    implicitHeight: 34
    height: 34
    width: parent ? parent.width : 170

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
        spacing: 9

        IconImage {
            name: root.icon
            size: 16
            tint: root.active ? Theme.accent : Theme.textSub
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            color: root.active ? Theme.text : Theme.textSub
            font.pixelSize: 13
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
