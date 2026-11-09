import QtQuick
import QtQuick.Controls.Basic
import XMF

// Compact square checkbox matching the console density.
CheckBox {
    id: root
    font.pixelSize: 10
    spacing: 6
    leftPadding: 0

    indicator: Rectangle {
        implicitWidth: 14
        implicitHeight: 14
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        radius: 2
        color: root.checked ? Theme.accent : "transparent"
        border.width: 1
        border.color: root.checked ? Theme.accent : Theme.borderStrong

        IconImage {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"
            size: 10
            tint: Theme.textOnAccent
        }
    }

    contentItem: Text {
        leftPadding: root.indicator.width + root.spacing
        text: root.text
        color: root.checked ? Theme.text : Theme.textSub
        font.pixelSize: 10
        verticalAlignment: Text.AlignVCenter
    }
}
