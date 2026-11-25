import QtQuick
import XMF

// Small floating task panel at the bottom-right corner.
Item {
    id: dock
    width: 300
    height: activeColumn.height + 54
    visible: App.activeTasks.length > 0
    z: 900

    function overallProgress() {
        if (App.batchState && App.batchState.active)
            return App.batchState.percent || 0
        const tasks = App.activeTasks || []
        if (tasks.length === 0)
            return 0
        let sum = 0, n = 0
        for (let i = 0; i < tasks.length; ++i) {
            const p = Number(tasks[i].progress)
            if (p >= 0) { sum += p; n += 1 }
        }
        return n > 0 ? Math.round(sum / n) : 0
    }

    function speedOf(task) {
        const m = String(task && task.status ? task.status : "").match(/([0-9.]+)\s*MB\/s/)
        return m ? (m[1] + " MB/s") : ""
    }

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
                    font.pixelSize: Theme.fs(11)
                    elide: Text.ElideRight
                    width: parent.width - 46
                }
                Text {
                    anchors.right: parent.right
                    text: modelData.progress >= 0 ? modelData.progress + "%" : "…"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fs(10)
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
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: dock.speedOf(modelData)
                    color: Theme.accent
                    font.pixelSize: Theme.fs(9)
                }
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    width: parent.width - 90
                    text: modelData.status || ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fs(9)
                    elide: Text.ElideRight
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

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        height: 14
        color: "transparent"

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "总进度"
            color: Theme.textMuted
            font.pixelSize: Theme.fs(9)
        }
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 44
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 4
            color: Theme.divider
            Rectangle {
                width: parent.width * dock.overallProgress() / 100
                height: parent.height
                color: Theme.success
                Behavior on width { NumberAnimation { duration: Theme.durFast } }
            }
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 2
            text: dock.overallProgress() + "%"
            color: Theme.textSub
            font.pixelSize: Theme.fs(9)
        }
    }
}