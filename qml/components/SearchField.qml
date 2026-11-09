import QtQuick
import QtQuick.Controls.Basic
import XMF

// Flat search input with 1px border.
Rectangle {
    id: root
    property alias text: field.text
    property string placeholder: "搜索"

    implicitHeight: 24
    implicitWidth: 200
    radius: Theme.r10
    color: Theme.panel
    border.width: 1
    border.color: field.activeFocus ? Theme.accent : Theme.border

    IconImage {
        id: icon
        anchors.left: parent.left
        anchors.leftMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: 12
        tint: Theme.textMuted
    }

    TextField {
        id: field
        anchors.left: icon.right
        anchors.leftMargin: 5
        anchors.right: parent.right
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.text
        placeholderText: root.placeholder
        placeholderTextColor: Theme.textMuted
        font.pixelSize: 11
        selectByMouse: true
        background: null
        padding: 0
        onAccepted: root.accepted()
    }

    signal accepted()
}
