import QtQuick
import XMF

// Classic checkbox (square box + check mark). API kept compatible with the old
// switch component so existing pages keep working.
Item {
    id: root
    property bool checked: false
    property color accentColor: Theme.accent
    signal toggled(bool value)

    implicitWidth: 16
    implicitHeight: 16

    Rectangle {
        anchors.fill: parent
        radius: Theme.r6
        color: root.checked ? root.accentColor : Theme.panel
        border.width: 1
        border.color: root.checked ? root.accentColor : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: 80 } }

        IconImage {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"
            size: 11
            tint: Theme.textOnAccent
        }
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
