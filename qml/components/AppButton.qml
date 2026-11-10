import QtQuick
import XMF

// Industrial push button: sharp 2px corners, amber primary, dense heights.
Item {
    id: root

    property string text: ""
    property string icon: ""
    property int kind: 0        // 0 = primary, 1 = normal, 2 = flat, 3 = danger
    property bool small: false
    property bool busy: false
    property bool enabled: true
    property bool onDark: false
    signal clicked()

    implicitHeight: small ? 26 : 30
    implicitWidth: row.implicitWidth + (small ? 22 : 30)
    opacity: enabled ? 1 : 0.45
    scale: mouse.pressed && enabled ? 0.965 : (mouse.containsMouse && enabled ? 1.012 : 1.0)
    transformOrigin: Item.Center
    Behavior on scale { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.r6
        color: {
            if (root.kind === 0)
                return mouse.pressed ? Theme.accentHover : Theme.accent
            if (root.kind === 3)
                return mouse.containsMouse ? Theme.alpha(Theme.danger, 0.14) : Theme.panel
            if (root.kind === 2)
                return mouse.containsMouse ? (root.onDark ? "#3B372E" : Theme.panelHover) : "transparent"
            if (root.onDark)
                return mouse.containsMouse ? "#3B372E" : "transparent"
            return mouse.containsMouse ? (mouse.pressed ? Theme.panelAlt : Theme.panelHover) : Theme.panel
        }
        border.width: root.kind === 2 || root.onDark ? 0 : 1
        border.color: root.kind === 3 ? Theme.alpha(Theme.danger, 0.55)
                      : (root.kind === 0 ? Theme.accentHover : Theme.borderStrong)
        Behavior on color { ColorAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        IconImage {
            visible: root.icon !== ""
            name: root.icon
            size: root.small ? 13 : 14
            tint: root.kind === 0 ? Theme.textOnAccent
                  : (root.kind === 3 ? Theme.danger : (root.onDark ? Theme.sectionText : Theme.textSub))
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.text !== ""
            text: root.busy ? "执行中…" : root.text
            color: root.kind === 0 ? Theme.textOnAccent
                   : (root.kind === 3 ? Theme.danger : (root.onDark ? Theme.sectionText : Theme.text))
            font.pixelSize: root.small ? 12 : 13
            font.weight: root.kind === 0 ? Font.DemiBold : Font.Normal
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
