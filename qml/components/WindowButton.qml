import QtQuick
import XMF

// Windows-style caption button: 40x26 square hover highlight.
Item {
    id: root
    property int kind: 0   // 0 minimize, 1 maximize, 2 close
    property bool hovered: false
    implicitWidth: 40
    implicitHeight: 26

    Rectangle {
        anchors.fill: parent
        color: root.hovered
               ? (root.kind === 2 ? "#C42B1C" : Theme.panelHover)
               : "transparent"
        Behavior on color { ColorAnimation { duration: 80 } }
    }

    Rectangle {
        visible: root.kind === 0
        width: 10; height: 1
        color: root.kind === 2 && root.hovered ? "#FFFFFF" : Theme.text
        anchors.centerIn: parent
    }

    Rectangle {
        visible: root.kind === 1
        width: 9; height: 9
        color: "transparent"
        border.width: 1
        border.color: Theme.text
        anchors.centerIn: parent
    }

    Item {
        visible: root.kind === 2
        width: 10; height: 10
        anchors.centerIn: parent
        Rectangle { width: 10; height: 1; color: "#FFFFFF"; anchors.centerIn: parent; rotation: 45 }
        Rectangle { width: 10; height: 1; color: "#FFFFFF"; anchors.centerIn: parent; rotation: -45 }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: root.hovered = true
        onExited: root.hovered = false
        onClicked: root.clicked()
    }

    signal clicked()
}
