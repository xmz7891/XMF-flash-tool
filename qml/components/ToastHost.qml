import QtQuick
import XMF

// Small flat toast message shown at the bottom of the window.
Item {
    id: host
    anchors.fill: parent
    z: 2000

    function show(type, message) {
        var color = Theme.textSub
        var icon = "info"
        if (type === "success") { color = Theme.success; icon = "circle-check-big" }
        else if (type === "warning") { color = Theme.warning; icon = "triangle-alert" }
        else if (type === "error") { color = Theme.danger; icon = "circle-x" }
        toastModel.append({ message: message, color: color, icon: icon })
        toastTimer.restart()
    }

    Timer {
        id: toastTimer
        interval: 2400
        onTriggered: if (toastModel.count > 0) toastModel.remove(0)
    }

    ListModel { id: toastModel }

    Column {
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 80
        spacing: 4
        z: 2

        Repeater {
            model: toastModel
            delegate: Rectangle {
                id: toast
                property color accentColor: model.color
                width: Math.min(520, Math.max(240, textItem.implicitWidth + 52))
                height: 28
                radius: Theme.r10
                color: Theme.darkMode ? "#F22A2A2D" : "#F2FFFFFF"
                border.width: 1
                border.color: Theme.border
                opacity: 0
                Component.onCompleted: { fadeIn.start(); lifetime.start() }
                NumberAnimation {
                    id: fadeIn
                    target: toast
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: Theme.durNormal
                    easing.type: Easing.OutCubic
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 6
                    IconImage {
                        name: model.icon
                        size: 13
                        tint: toast.accentColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: textItem
                        text: model.message
                        color: Theme.text
                        font.pixelSize: 11
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        width: parent.width - 28
                    }
                }

                Timer {
                    id: lifetime
                    interval: 2400
                    onTriggered: if (index >= 0 && index < toastModel.count) toastModel.remove(index)
                }
            }
        }
    }
}
