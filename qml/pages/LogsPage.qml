import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

Item {
    id: page

    property int levelFilter: -1
    property string search: ""
    property bool follow: true

    function levelColor(level) {
        if (level === 2) return Theme.success
        if (level === 3) return Theme.warning
        if (level === 4) return Theme.danger
        if (level === 0) return Theme.textMuted
        return Theme.textSub
    }

    FileDialog {
        id: exportDialog
        title: "导出日志"
        nameFilters: ["日志文件 (*.log *.txt)", "所有文件 (*.*)"]
        onAccepted: {
            const path = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            const saved = App.saveLogs(path)
            if (saved.length > 0)
                App.notify("success", "日志已导出: " + saved)
            else
                App.notify("error", "日志导出失败")
        }
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 12

        PageHeader {
            width: parent.width
            title: "运行日志"
            subtitle: "所有工具、设备与刷写流程的实时输出"
            icon: "file-text"
            AppButton {
                text: "导出"
                icon: "download"
                kind: 1
                small: true
                onClicked: exportDialog.open()
            }
            AppButton {
                text: "复制"
                icon: "copy"
                kind: 1
                small: true
                onClicked: {
                    App.logs.copyToClipboard()
                    App.notify("success", "日志已复制到剪贴板")
                }
            }
            AppButton {
                text: "清空"
                icon: "trash-2"
                kind: 2
                small: true
                onClicked: App.clearLogs()
            }
            SearchField {
                width: 200
                placeholder: "过滤日志"
                onTextChanged: page.search = text
            }
        }

        Row {
            spacing: 6
            Repeater {
                model: [
                    { level: -1, name: "全部" },
                    { level: 1, name: "信息" },
                    { level: 2, name: "成功" },
                    { level: 3, name: "警告" },
                    { level: 4, name: "错误" }
                ]
                delegate: Rectangle {
                    height: 26
                    width: chipTxt.implicitWidth + 22
                    radius: 7
                    color: page.levelFilter === modelData.level ? Theme.accent : "transparent"
                    border.width: 1
                    border.color: page.levelFilter === modelData.level ? Theme.accent : Theme.border

                    Text {
                        id: chipTxt
                        anchors.centerIn: parent
                        text: modelData.name
                        color: page.levelFilter === modelData.level ? Theme.textOnAccent : Theme.textSub
                        font.pixelSize: 10
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.levelFilter = modelData.level
                    }
                }
            }
        }

        GlassSurface {
            width: parent.width
            height: parent.height - 116
            radius: Theme.r18
            color: Theme.darkMode ? "#59000000" : "#59FFFFFF"

            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: App.logs
                spacing: 1
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    width: logView.width
                    height: visible ? 26 : 0
                    visible: (page.levelFilter < 0 || model.level === page.levelFilter)
                             && (page.search === "" || model.message.toLowerCase().indexOf(page.search.toLowerCase()) >= 0
                                 || model.category.toLowerCase().indexOf(page.search.toLowerCase()) >= 0)

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: rowMouse.containsMouse ? Theme.glass : "transparent"
                    }
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10

                        Text {
                            text: model.time
                            color: Theme.textMuted
                            font.pixelSize: 10
                            font.family: "Consolas"
                            width: 78
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            width: 52
                            height: 16
                            radius: 4
                            color: Theme.alpha(page.levelColor(model.level), 0.12)
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                anchors.centerIn: parent
                                text: model.category
                                color: page.levelColor(model.level)
                                font.pixelSize: 8
                                elide: Text.ElideRight
                                width: parent.width - 6
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                        Text {
                            width: parent.width - 200
                            text: model.message
                            color: model.level >= 3 ? page.levelColor(model.level) : Theme.textSub
                            font.pixelSize: 11
                            font.family: "Consolas"
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }

                onCountChanged: if (page.follow) positionViewAtEnd()
            }
        }
    }

    Connections {
        target: App.logs
        function onAppended() { if (page.follow) logView.positionViewAtEnd() }
    }
}
