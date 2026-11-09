import QtQuick
import QtQuick.Controls.Basic
import XMF

Item {
    id: page
    signal openEmbedded(string toolId)

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 14

        PageHeader {
            width: parent.width
            title: "官方工具"
            subtitle: "随程序内置的厂商官方刷机工具与驱动，可作为独立后端直接启动"
            icon: "box"
            AppButton {
                text: "打开工具目录"
                icon: "folder-open"
                kind: 1
                small: true
                onClicked: App.officialTools.openRoot()
            }
            AppButton {
                text: "重新扫描"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: App.officialTools.refresh()
            }
        }

        Flickable {
            width: parent.width
            height: parent.height - 78
            contentHeight: contentColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            Column {
                id: contentColumn
                width: parent.width
                spacing: 12

                Flow {
                    width: parent.width
                    spacing: 12
                    height: childrenRect.height

                    Repeater {
                        model: App.officialTools.tools
                        delegate: GlassSurface {
                            width: (page.width - 24) / 3
                            height: 132
                            radius: Theme.r18
                            interactive: true
                            color: hovered ? Theme.glassHover : Theme.glass

                            Column {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 6

                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Rectangle {
                                        width: 34; height: 34
                                        radius: 10
                                        color: Theme.alpha(Theme.text, 0.06)
                                        border.width: 1
                                        border.color: Theme.border
                                        IconImage {
                                            anchors.centerIn: parent
                                            name: modelData.icon || "box"
                                            size: 16
                                            tint: Theme.textSub
                                        }
                                    }
                                    Column {
                                        width: parent.width - 44
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 1
                                        Text {
                                            text: modelData.name
                                            color: Theme.text
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            width: parent.width
                                        }
                                        Text {
                                            text: modelData.kind === "cli" ? "命令行后端" : "图形工具"
                                            color: Theme.textMuted
                                            font.pixelSize: 9
                                        }
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.description
                                    color: Theme.textMuted
                                    font.pixelSize: 9
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                Item { width: 1; height: 2 }

                                Row {
                                    width: parent.width
                                    spacing: 8
                                    Rectangle {
                                        width: 7; height: 7
                                        radius: 3.5
                                        color: modelData.available ? Theme.success : Theme.textMuted
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: modelData.available ? "已内置" : "文件缺失"
                                        color: modelData.available ? Theme.textSub : Theme.textMuted
                                        font.pixelSize: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Item { width: Math.max(0, parent.width - 190); height: 1 }
                                    AppButton {
                                        visible: modelData.kind !== "cli"
                                        text: "内嵌启动"
                                        kind: 1
                                        small: true
                                        height: 24
                                        enabled: modelData.available
                                        onClicked: page.openEmbedded(modelData.id)
                                    }
                                    AppButton {
                                        text: "目录"
                                        kind: 2
                                        small: true
                                        height: 24
                                        onClicked: App.officialTools.openFolder(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                GlassSurface {
                    width: parent.width
                    height: 92
                    radius: Theme.r18

                    Row {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 14
                        IconImage {
                            name: "hard-drive"
                            size: 16
                            tint: Theme.textSub
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Column {
                            width: parent.width - 200
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                text: "随附驱动包"
                                color: Theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                text: App.officialTools.drivers.length + " 组驱动（Qualcomm 9008 / Google ADB / MTK / 展锐等），可通过「安装驱动」调用 pnputil 安装，需管理员权限。"
                                color: Theme.textMuted
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }
                        Item { width: 1; height: 1 }
                        AppButton {
                            text: "安装驱动"
                            icon: "download"
                            kind: 1
                            small: true
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                if (App.installDrivers())
                                    App.notify("success", "驱动安装完成")
                            }
                        }
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: App.officialTools.drivers
                        delegate: GlassSurface {
                            width: (page.width - 20) / 3
                            height: 66
                            radius: Theme.r14

                            Row {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10
                                IconImage {
                                    name: "usb"
                                    size: 14
                                    tint: Theme.textSub
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Text {
                                        text: modelData.vendor
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: modelData.infCount + " 个 inf"
                                        color: Theme.textMuted
                                        font.pixelSize: 9
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.openPath(modelData.path)
                            }
                        }
                    }
                }

                GlassSurface {
                    width: parent.width
                    height: 76
                    radius: Theme.r18

                    Row {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        IconImage {
                            name: "info"
                            size: 15
                            tint: Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: parent.width - 40
                            text: "官方工具仅作为兼容后端随包附带，版权归各厂商所有；主程序的「内置模式」工作台仍以单窗口方式完成同一套流程，无需打开外部程序。"
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
    }
}
