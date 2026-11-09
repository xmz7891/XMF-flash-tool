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
                MouseArea {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: 14; height: 14
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.cancelTask(modelData.id)
                    IconImage { anchors.centerIn: parent; name: "x"; size: 9; tint: Theme.textMuted }
                }
            }
        }
    }
}
