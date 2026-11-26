import QtQuick
import QtQuick.Controls.Basic
import XMF

MenuItem {
    id: root
    implicitHeight: 32
    leftPadding: 12
    rightPadding: 12

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Theme.textDim
                             : (root.highlighted ? Theme.accent : Theme.text)
        font.pixelSize: Theme.fs(11)
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
        rightPadding: 12
        elide: Text.ElideRight
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 8
        color: root.highlighted ? Theme.panelHover : "transparent"
    }
}
