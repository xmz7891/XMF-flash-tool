import QtQuick
import XMF

// Larger settings switch: clear visual state, aligned in the control column.
Item {
    id: root
    property bool checked: false
    signal toggled(bool value)

    implicitWidth: 44
    implicitHeight: 24

    Rectangle {
        id: track
        anchors.fill: parent
        radius: 12
        color: root.checked ? Theme.accent : Theme.panelAlt
        border.width: 1
        border.color: root.checked ? Theme.accent : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durNormal; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: knob
        width: 18; height: 18
        radius: 9
        y: 3
        x: root.checked ? parent.width - width - 3 : 3
        color: root.checked ? "#FFFFFF" : Theme.textSub
        scale: mouse.pressed ? 0.88 : 1.0
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }
        Behavior on x {
            enabled: Theme.animations
            SpringAnimation { spring: 4.8; damping: 0.34; mass: 1.0; epsilon: 0.001 }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
}
