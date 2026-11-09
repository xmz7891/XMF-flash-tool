import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// Settings board: two-column instrument layout, compact rows, dark section
// headers and controls aligned in a consistent column.
Item {
    id: page

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height + 40
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: contentColumn
            x: 16
            width: parent.width - 32
            y: 12
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

            Row {
                width: parent.width
                height: 242
                spacing: 12

                GlassSurface {
                    width: parent.width * 0.58 - 6
                    height: 242
                    title: "外观"
                    hint: "主题"

                    Column {
                        anchors.fill: parent
                        spacing: 0

                        SettingsRow {
                            width: parent.width
                            title: "界面动画"
                            desc: "页面切换与进度动画"

                            SettingToggle {
                                checked: App.settings.animations
                                onToggled: App.settings.animations = value
                            }
                        }

                        SettingsRow {
                            width: parent.width
                            title: "缓存与自检"
                            desc: "缓存 " + App.formatBytes(App.cacheSize()) + " · 校验内置工具"

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

                        SettingsRow {
                            width: parent.width
                            title: "日志级别"
                            desc: "低于该级别不再记录"

                            ComboBox {
                                id: logLevelCombo
                                width: 128
                                height: 26
                                model: ["debug", "info", "warning", "error"]
                                currentIndex: Math.max(0, model.indexOf(App.settings.logLevel))
                                onActivated: App.settings.logLevel = model[index]
                                font.pixelSize: 11
                                background: Rectangle {
                                    radius: Theme.r6
                                    color: Theme.panelAlt
                                    border.width: 1
                                    border.color: Theme.borderStrong
                                }
                                indicator: IconImage {
                                    x: logLevelCombo.width - 18
                                    y: 7
                                    name: "chevron-down"
                                    size: 12
                                    tint: Theme.textMuted
                                }
                                contentItem: Text {
                                    leftPadding: 8
                                    text: logLevelCombo.displayText
                                    color: Theme.text
                                    font.pixelSize: 11
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        SettingsRow {
                            width: parent.width
                            title: "外观主题"
                            desc: "浅色 / 深色 / 跟随系统"
                            showDivider: false

                            AppButton {
                                text: "深色"
                                kind: App.settings.themeMode === "dark" ? 0 : 1
                                small: true
                                onClicked: App.settings.themeMode = "dark"
                            }
                            AppButton {
                                text: "浅色"
                                kind: App.settings.themeMode === "light" ? 0 : 1
                                small: true
                                onClicked: App.settings.themeMode = "light"
                            }
                            AppButton {
                                text: "跟随系统"
                                kind: App.settings.themeMode === "auto" ? 0 : 1
                                small: true
                                onClicked: App.settings.themeMode = "auto"
                            }
                        }
                    }
                }

                Column {
                    width: parent.width * 0.42 - 6
                    height: parent.height
                    spacing: 12

                    GlassSurface {
                        width: parent.width
                        height: 138
                        title: "刷机行为"
                        hint: "写入确认"

                        Column {
                            anchors.fill: parent
                            spacing: 0

                            SettingsRow {
                                width: parent.width
                                title: "危险操作二次确认"
                                desc: "擦除、格机前确认"

                                SettingToggle {
                                    checked: App.settings.confirmDangerous
                                    onToggled: App.settings.confirmDangerous = value
                                }
                            }

                            SettingsRow {
                                width: parent.width
                                title: "自动刷新端口"
                                desc: "插拔设备时自动刷新"
                                showDivider: false

                                SettingToggle {
                                    checked: App.settings.autoRefreshPorts
                                    onToggled: App.settings.autoRefreshPorts = value
                                }
                            }
                        }
                    }

                    GlassSurface {
                        width: parent.width
                        height: 92
                        title: "下载目录"

                        Item {
                            anchors.fill: parent

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: "下载目录"
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                TextField {
                                    width: 196
                                    height: 26
                                    readOnly: true
                                    text: App.settings.downloadDir
                                    color: Theme.textSub
                                    font.pixelSize: 10
                                    selectByMouse: true
                                    leftPadding: 8
                                    rightPadding: 8
                                    background: Rectangle {
                                        radius: Theme.r6
                                        color: Theme.panelAlt
                                        border.width: 1
                                        border.color: Theme.borderStrong
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
                }
            }

            GlassSurface {
                width: parent.width
                height: 92

                Row {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    Rectangle {
                        width: 42; height: 42
                        radius: Theme.r6
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
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: "XMF Flash Tool  v" + App.version
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Text {
                            text: "C++20 · Qt 6.8 · Windows 10 / 11"
                            color: Theme.textMuted
                            font.pixelSize: 10
                        }
                    }
                    Item { width: Math.max(0, parent.width - 640); height: 1 }
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
                        onClicked: App.openUrl("https://github.com/xmz7891/XMF-flash-tool/releases/latest")
                    }
                }
            }
        }
    }

    FolderDialog {
        id: dirDialog
        title: "选择固件下载目录"
        onAccepted: App.settings.downloadDir =
                    String(selectedFolder).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
    }
}
