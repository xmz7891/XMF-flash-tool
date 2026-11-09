import QtQuick
import XMF

// Classic desktop push button: 1px border, light fill, 3px radius.
Item {
    id: root

    property string text: ""
    property string icon: ""
    property int kind: 0        // 0 = primary, 1 = normal, 2 = flat, 3 = danger
    property bool small: false
    property bool busy: false
    property bool enabled: true
    signal clicked()

    implicitHeight: small ? 24 : 28
    implicitWidth: row.implicitWidth + (small ? 18 : 26)
    opacity: enabled ? 1 : 0.5

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.r10
        color: {
            if (root.kind === 0)
                return mouse.pressed ? Theme.accentHover : Theme.accent
            if (root.kind === 3)
                return mouse.containsMouse ? Theme.alpha(Theme.danger, 0.12) : Theme.panel
            if (root.kind === 2)
                return mouse.containsMouse ? Theme.panelHover : "transparent"
            return mouse.containsMouse ? (mouse.pressed ? Theme.panelAlt : Theme.panelHover) : Theme.panel
        }
        border.width: root.kind === 2 ? 0 : 1
        border.color: root.kind === 3 ? Theme.alpha(Theme.danger, 0.5)
                      : (root.kind === 0 ? Theme.accentHover : Theme.border)
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        IconImage {
            visible: root.icon !== ""
            name: root.icon
            size: root.small ? 12 : 13
            tint: root.kind === 0 ? Theme.textOnAccent : (root.kind === 3 ? Theme.danger : Theme.textSub)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.text !== ""
            text: root.busy ? "执行中…" : root.text
            color: root.kind === 0 ? Theme.textOnAccent : (root.kind === 3 ? Theme.danger : Theme.text)
            font.pixelSize: root.small ? 11 : 12
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: root.enabled && !root.busy
        onClicked: root.clicked()
    }
}
