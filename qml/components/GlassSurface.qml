import QtQuick
import XMF

// Flat desktop panel: 1px border, optional title strip, dense spacing.
Rectangle {
    id: root
    radius: Theme.r14
    color: Theme.panel
    border.width: 1
    border.color: Theme.border

    property bool interactive: false
    property bool hovered: false
    property alias title: titleText.text
    property color accentColor: Theme.accent
    property real highlight: 0.0
    property int titleHeight: 30
    default property alias contentData: body.data

    Behavior on color {
        ColorAnimation { duration: Theme.durFast }
    }

    // optional group title strip (set `title` to show it)
    Rectangle {
        id: titleStrip
        visible: titleText.text !== ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.titleHeight
        color: Theme.panelAlt
        radius: root.radius

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.divider
        }

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    Item {
        id: body
        anchors.top: titleStrip.visible ? titleStrip.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
        clip: true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: root.interactive
        enabled: root.interactive
        onEntered: root.hovered = true
        onExited: root.hovered = false
    }
}
