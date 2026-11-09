import QtQuick
import XMF

// Windows-style caption button: 40x26 square hover highlight.
Item {
    id: root
    property int kind: 0   // 0 minimize, 1 maximize, 2 close
    property bool hovered: false
    implicitWidth: 40
    implicitHeight: 26

    readonly property color ink: root.hovered && root.kind === 2 ? "#FFFFFF" : Theme.textSub

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
        color: root.ink
        anchors.centerIn: parent
    }

    IconImage {
        visible: root.kind === 1
        anchors.centerIn: parent
        name: "square"
        size: 9
        tint: root.ink
    }

    IconImage {
        visible: root.kind === 2
        anchors.centerIn: parent
        name: "x"
        size: 10
        tint: root.ink
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
