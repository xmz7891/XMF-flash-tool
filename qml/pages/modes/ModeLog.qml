import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import XMF

// 模式工作台的运行日志面板：等宽字体、级别色点、自动滚动、复制 / 清空。
// 通过 append(level, message) 写入；level 取 info / success / warning / error / debug。
GlassSurface {
    id: root

    property var seedLines: []
    property bool follow: true
    property int maxLines: 400
    property int lineCount: 0

    radius: Theme.r18
    color: Theme.glass

    ListModel { id: lineModel }

    function levelOf(name) {
        if (name === "success")
            return 1
        if (name === "warning")
            return 2
        if (name === "error")
            return 3
        if (name === "debug")
            return 4
        return 0
    }

    function colorOf(level) {
        if (level === 1)
            return Theme.success
        if (level === 2)
            return Theme.warning
        if (level === 3)
            return Theme.danger
        if (level === 4)
            return Theme.textMuted
        return root.accentColor
    }

    function append(level, message) {
        lineModel.append({
            level: levelOf(level),
            time: Qt.formatTime(new Date(), "HH:mm:ss"),
            message: message
        })
        if (lineModel.count > maxLines)
            lineModel.remove(0)
        lineCount = lineModel.count
        if (follow)
            view.positionViewAtEnd()
    }

    function clearAll() {
        lineModel.clear()
        lineCount = 0
    }

    function allText() {
        let out = ""
        for (let i = 0; i < lineModel.count; ++i) {
            const item = lineModel.get(i)
            out += item.time + "  " + item.message + "\n"
        }
        return out
    }

    function seedSample() {
        for (let i = 0; i < seedLines.length; ++i) {
            const s = seedLines[i] || {}
            lineModel.append({
                level: levelOf(s.level),
                time: s.time || Qt.formatTime(new Date(Date.now() - (seedLines.length - i) * 1700), "HH:mm:ss"),
                message: s.message || s.text || ""
            })
        }
        lineCount = lineModel.count
        view.positionViewAtEnd()
    }

    Component.onCompleted: seedSample()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            spacing: 8

            IconImage {
                Layout.alignment: Qt.AlignVCenter
                name: "terminal"
                size: 13
                tint: root.accentColor
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: "运行日志"
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: lineModel.count + " 行"
                color: Theme.textMuted
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 66
                Layout.preferredHeight: 20
                radius: 10
                color: root.follow ? Theme.accentSoft : "transparent"
                border.width: 1
                border.color: root.follow ? Theme.borderStrong : Theme.border

                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

                Text {
                    anchors.centerIn: parent
                    text: "自动滚动"
                    color: root.follow ? root.accentColor : Theme.textMuted
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.follow = !root.follow
                        if (root.follow)
                            view.positionViewAtEnd()
                    }
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 7
                color: copyMouse.containsMouse ? Theme.glassHover : "transparent"

                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                IconImage {
                    anchors.centerIn: parent
                    name: "copy"
                    size: 11
                    tint: copyMouse.containsMouse ? Theme.text : Theme.textMuted
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        App.copyText(root.allText())
                        App.notify("success", "本页日志已复制")
                    }
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 7
                color: clearMouse.containsMouse ? Theme.glassHover : "transparent"

                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                IconImage {
                    anchors.centerIn: parent
                    name: "trash-2"
                    size: 11
                    tint: clearMouse.containsMouse ? Theme.danger : Theme.textMuted
                }

                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.clearAll()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 40
            clip: true
            model: lineModel
            spacing: 1
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            onCountChanged: if (root.follow) positionViewAtEnd()

            delegate: Item {
                width: view.width
                height: 21

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: hoverArea.containsMouse ? Theme.glass : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 8

                    Text {
                        width: 54
                        text: model.time
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.family: "Consolas"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        width: 4
                        height: 4
                        radius: 2
                        color: root.colorOf(model.level)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: parent.width - 78
                        text: model.message
                        color: model.level >= 2 ? root.colorOf(model.level) : Theme.textSub
                        font.pixelSize: 10
                        font.family: "Consolas"
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }
        }
    }
}
