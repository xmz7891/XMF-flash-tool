import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

Item {
    id: page

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 12

        PageHeader {
            width: parent.width
            title: "设置"
            subtitle: "外观、日志与刷机行为"
            icon: "settings"
            AppButton {
                text: "打开数据目录"
                icon: "folder-open"
                kind: 1
                small: true
                onClicked: App.openPath(App.dataPath)
            }
        }

        Flickable {
            width: parent.width
            height: parent.height - 72
            contentHeight: settingsColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            Column {
                id: settingsColumn
                width: parent.width - 2
                spacing: 12

                // ------------------------------------------------ appearance
                GlassSurface {
                    width: parent.width
                    height: 210
                    radius: Theme.r18

                    Column {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 12

                        Text {
                            text: "外观"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Row {
                            width: parent.width
                            spacing: 12
                            Column {
                                width: parent.width - 180
                                spacing: 3
                                Text { text: "界面动画"; color: Theme.text; font.pixelSize: 12 }
                                Text { text: "页面切换、悬停与进度动画"; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings.animations
                                onToggled: function(value) { App.settings.animations = value }
                            }
                        }


                        Row {
                            width: parent.width
                            spacing: 12
                            Column {
                                width: parent.width - 320
                                spacing: 3
                                Text { text: "缓存与自检"; color: Theme.text; font.pixelSize: 12 }
                                Text {
                                    text: "引导包解压缓存 " + App.formatBytes(App.cacheSize())
                                          + " · 自检可验证内置工具与引导包是否完整"
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }
                            Row {
                                spacing: 6
                                anchors.verticalCenter: parent.verticalCenter
                                AppButton {
                                    text: "运行自检"
                                    kind: 1
                                    small: true
                                    onClicked: {
                                        if (App.runTool("system.selfTest", ({})) > 0)
                                            App.notify("info", "环境自检已开始，结果见日志")
                                    }
                                }
                                AppButton {
                                    text: "清理缓存"
                                    kind: 1
                                    small: true
                                    onClicked: {
                                        if (App.clearCache())
                                            App.notify("success", "缓存已清理")
                                        else
                                            App.notify("error", "缓存清理失败")
                                    }
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 12
                            Column {
                                width: parent.width - 320
                                spacing: 3
                                Text { text: "日志级别"; color: Theme.text; font.pixelSize: 12 }
                                Text {
                                    text: "低于所选级别的日志不再记录（debug / info / warning / error）"
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }
                            ComboBox {
                                id: logLevelCombo
                                width: 140
                                height: 26
                                anchors.verticalCenter: parent.verticalCenter
                                model: ["debug", "info", "warning", "error"]
                                currentIndex: Math.max(0, model.indexOf(App.settings.logLevel))
                                onActivated: App.settings.logLevel = model[index]
                                font.pixelSize: 11
                                background: Rectangle {
                                    radius: Theme.r10
                                    color: Theme.panel
                                    border.width: 1
                                    border.color: Theme.border
                                }
                                indicator: IconImage { x: logLevelCombo.width - 18; y: 7; name: "chevron-down"; size: 12; tint: Theme.textMuted }
                                contentItem: Text {
                                    leftPadding: 6
                                    text: logLevelCombo.displayText
                                    color: Theme.text
                                    font.pixelSize: 11
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            Item { width: 1; height: 1 }
                        }

                        Row {
                            width: parent.width
                            spacing: 12
                            Column {
                                width: parent.width - 320
                                spacing: 3
                                Text { text: "外观主题"; color: Theme.text; font.pixelSize: 12 }
                                Text { text: "浅色或深色，也可跟随系统"; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            Row {
                                spacing: 6
                                anchors.verticalCenter: parent.verticalCenter
                                Repeater {
                                    model: [
                                        { id: "dark", label: "深色" },
                                        { id: "light", label: "浅色" },
                                        { id: "auto", label: "跟随系统" }
                                    ]
                                    delegate: Rectangle {
                                        height: 30
                                        width: modeLabel.implicitWidth + 26
                                        radius: 8
                                        color: App.settings.themeMode === modelData.id ? Theme.accent : "transparent"
                                        border.width: 1
                                        border.color: App.settings.themeMode === modelData.id ? Theme.accent : Theme.border
                                        Behavior on color { ColorAnimation { duration: Theme.durFast } }

                                        Text {
                                            id: modeLabel
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: App.settings.themeMode === modelData.id ? Theme.textOnAccent : Theme.textSub
                                            font.pixelSize: 11
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: App.settings.themeMode = modelData.id
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ----------------------------------------------------- flash
                GlassSurface {
                    width: parent.width
                    height: 150
                    radius: Theme.r18

                    Column {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 12

                        Text {
                            text: "刷机行为"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Row {
                            width: parent.width
                            spacing: 12
                            Column {
                                width: parent.width - 180
                                spacing: 3
                                Text { text: "危险操作二次确认"; color: Theme.text; font.pixelSize: 12 }
                                Text { text: "擦除、格机等操作执行前需要确认"; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings.confirmDangerous
                                onToggled: function(value) { App.settings.confirmDangerous = value }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 12
                            Column {
                                width: parent.width - 180
                                spacing: 3
                                Text { text: "自动刷新端口"; color: Theme.text; font.pixelSize: 12 }
                                Text { text: "设备插拔时自动刷新端口列表"; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings.autoRefreshPorts
                                onToggled: function(value) { App.settings.autoRefreshPorts = value }
                            }
                        }
                    }
                }

                // ------------------------------------------------------ paths
                GlassSurface {
                    width: parent.width
                    height: 128
                    radius: Theme.r18

                    Column {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10

                        Text {
                            text: "固件下载目录"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Row {
                            width: parent.width
                            spacing: 10
                            Rectangle {
                                width: parent.width - 100
                                height: 36
                                radius: 10
                                color: Theme.glass
                                border.width: 1
                                border.color: Theme.border
                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    verticalAlignment: Text.AlignVCenter
                                    text: App.settings.downloadDir
                                    color: Theme.textSub
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                            }
                            AppButton {
                                text: "更改"
                                kind: 1
                                small: true
                                onClicked: dirDialog.open()
                            }
                        }
                    }
                }

                // ------------------------------------------------------ about
                GlassSurface {
                    width: parent.width
                    height: 120
                    radius: Theme.r18

                    Row {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 14

                        Rectangle {
                                width: 40; height: 40
                                radius: 8
                                clip: true
                                color: "transparent"
                                Image {
                                    anchors.fill: parent
                                    source: "qrc:/images/xmf_icon.png"
                                    fillMode: Image.PreserveAspectCrop
                                    smooth: true
                                }
                        }

                        Column {
                            spacing: 3
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: "XMF Flash Tool  v" + App.version
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: "C++20 · Qt 6.8 · Windows 10 / 11"
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }

                        Item { width: 1; height: 1 }
                        AppButton {
                            text: "日志目录"
                            icon: "file-text"
                            kind: 1
                            small: true
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: App.openPath(App.dataPath)
                        }
                        AppButton {
                            text: "检查更新"
                            icon: "refresh-cw"
                            kind: 1
                            small: true
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: App.notify("info", "当前已是最新版本")
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: dirDialog
        title: "选择固件下载目录"
        onAccepted: App.settings.downloadDir = String(selectedFolder).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
    }
}
