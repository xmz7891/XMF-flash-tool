import QtQuick
import XMF

// Flat desktop panel with a dark "instrument" header bar: amber marker,
// light title text and an optional right-hand hint. Sharp 3px corners.
Rectangle {
    id: root
    radius: Theme.r14
    color: Theme.panel
    border.width: 1
    border.color: Theme.border

    property bool interactive: false
    property bool hovered: false
    property alias title: titleText.text
    property string hint: ""
    property bool darkHeader: true
    property color accentColor: Theme.accent
    property real highlight: 0.0
    property int titleHeight: 34
    default property alias contentData: body.data

    Behavior on color {
        ColorAnimation { duration: Theme.durFast }
    }

    // header strip
    Rectangle {
        id: titleStrip
        visible: titleText.text !== ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.titleHeight
        color: root.darkHeader ? Theme.sectionBg : Theme.panelAlt
        radius: root.radius

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: root.darkHeader ? "#1F1E1A" : Theme.divider
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: 15
            radius: 1
            color: root.accentColor
        }

        Text {
            id: hintText
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width * 0.42, implicitWidth)
            text: root.hint
            color: root.darkHeader ? Theme.sectionMuted : Theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            visible: root.hint !== ""
        }

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(40, parent.width - anchors.leftMargin - 14
                            - (hintText.visible ? hintText.width + 10 : 0))
            text: root.title
            color: root.darkHeader ? Theme.sectionText : Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
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
