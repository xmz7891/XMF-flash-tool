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
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    Rectangle {
        id: knob
        width: 18; height: 18
        radius: 9
        y: 3
        x: root.checked ? parent.width - width - 3 : 3
        color: root.checked ? "#FFFFFF" : Theme.textSub
        Behavior on x { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
}
