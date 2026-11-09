import QtQuick
import XMF

// Small floating task panel at the bottom-right corner.
Item {
    id: dock
    width: 300
    height: activeColumn.height + 16
    visible: App.activeTasks.length > 0
    z: 900

    GlassSurface {
        anchors.fill: parent
        title: "后台任务"
    }

    Column {
        id: activeColumn
        width: parent.width - 16
        x: 8
        y: 34
        spacing: 4

        Repeater {
            model: App.activeTasks
            delegate: Item {
                width: activeColumn.width
                height: 40

                Text {
                    text: modelData.title
                    color: Theme.text
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width - 46
                }
                Text {
                    anchors.right: parent.right
                    text: modelData.progress >= 0 ? modelData.progress + "%" : "…"
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3
                    color: Theme.divider
                    Rectangle {
                        width: modelData.progress >= 0 ? parent.width * modelData.progress / 100 : parent.width * 0.3
                        height: parent.height
                        color: Theme.accent
                    }
                }
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: modelData.status || ""
                    color: Theme.textMuted
                    font.pixelSize: 9
                    elide: Text.ElideRight
                    width: parent.width - 60
                }
                // Explicit 20x20 hit target with hover feedback, so the cancel
                // button is never an invisible "ghost" area.
                Item {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 3
                    width: 20; height: 20

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.r10
                        color: cancelArea.containsMouse ? Theme.panelHover : "transparent"
                    }
                    IconImage {
                        anchors.centerIn: parent
                        name: "x"
                        size: 11
                        tint: cancelArea.containsMouse ? Theme.danger : Theme.textSub
                    }
                    MouseArea {
                        id: cancelArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.cancelTask(modelData.id)
                    }
                }
            }
        }
    }
}
