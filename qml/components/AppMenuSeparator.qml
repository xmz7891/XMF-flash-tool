import QtQuick
import QtQuick.Controls.Basic
import XMF

MenuSeparator {
    id: root
    implicitHeight: 9
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.divider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        anchors.rightMargin: 10
    }
}
