import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import XMF

Item {
    id: page

    signal openMode(string modeId)

    readonly property var modes: [
        { id: "fastboot", name: "Fastboot 模式", sub: "分区刷写（需已解锁 BL）", detail: "读取分区表、校验容量、刷写/擦除 boot、recovery 等分区，与 9008、Recovery 不同", icon: "zap", tag: "FASTBOOT" },
        { id: "recovery", name: "Recovery 模式", sub: "ADB Sideload 卡刷", detail: "进入 Recovery 后通过 sideload 刷入 OTA/第三方 zip，与 Fastboot 刷分区不同", icon: "rotate-ccw", tag: "RECOVERY" },
        { id: "qcom-edl", name: "Qualcomm 9008", sub: "Sahara + Firehose 深度刷机", detail: "9008 紧急下载模式，需要 Firehose programmer；可读写扇区、救砖，与 Fastboot/Recovery 不同", icon: "cpu", tag: "9008" },
        { id: "miflash", name: "MiFlash 模式", sub: "小米 / 红米 tgz 线刷包", detail: "全部删除、保留用户数据、删除并锁 BL", icon: "smartphone", tag: "MI" },
        { id: "spflash", name: "SP Flash Tool", sub: "联发科 Scatter 固件", detail: "全部下载、仅刷选定分区、格式化下载、分区回读", icon: "microchip", tag: "MTK" },
        { id: "spdflash", name: "SPD Flash Tool", sub: "紫光展锐 PAC 固件", detail: "PAC 解析、FDL 握手、分区刷写与备份", icon: "layers", tag: "SPRD" },
        { id: "rkdevtool", name: "RKDevTool", sub: "瑞芯微固件与分区镜像", detail: "MaskROM / Loader 设备识别、分区烧录与回读", icon: "box", tag: "RK" },
        { id: "phoenixsuit", name: "PhoenixSuit", sub: "全志固件镜像", detail: "FEL 模式识别、一键刷机与分区读写", icon: "sun", tag: "AW" }
    ]

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 14

        PageHeader {
            width: parent.width
            title: "内置模式"
            subtitle: "经典刷机工具的工作台，全部在主窗口内完成"
            icon: "layout-dashboard"
            AppButton {
                text: "模式说明"
                icon: "circle-help"
                kind: 1
                small: true
                onClicked: App.notify("info", "各工作台共享设备端口、日志与任务系统")
            }
        }

        Flickable {
            width: parent.width
            height: parent.height - 76
            contentHeight: modesColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            Column {
                id: modesColumn
                width: parent.width
                spacing: 10

                Repeater {
                    model: page.modes
                    delegate: GlassSurface {
                        width: modesColumn.width
                        height: 88
                        radius: Theme.r18
                        interactive: true
                        color: hovered ? Theme.glassHover : Theme.glass

                        Row {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 14

                            Rectangle {
                                width: 46; height: 46
                                radius: 13
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.alpha(Theme.text, 0.06)
                                border.width: 1
                                border.color: Theme.border
                                IconImage {
                                    anchors.centerIn: parent
                                    name: modelData.icon
                                    size: 21
                                    tint: Theme.textSub
                                }
                            }

                            Column {
                                width: parent.width - 46 - 14 - 230
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                Row {
                                    spacing: 8
                                    Text {
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }
                                    Rectangle {
                                        width: tagLabel.implicitWidth + 12
                                        height: 15
                                        radius: 4
                                        color: Theme.glassStrong
                                        border.width: 1
                                        border.color: Theme.border
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            id: tagLabel
                                            anchors.centerIn: parent
                                            text: modelData.tag
                                            color: Theme.textMuted
                                            font.pixelSize: 8
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                                Text {
                                    text: modelData.sub
                                    color: Theme.textSub
                                    font.pixelSize: 10
                                }
                                Text {
                                    text: modelData.detail
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }

                            Item { width: 1; height: 1 }

                            AppButton {
                                text: "打开工作台"
                                kind: 1
                                small: true
                                width: 118
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: page.openMode(modelData.id)
                            }
                        }
                    }
                }

                GlassSurface {
                    width: parent.width
                    height: 68
                    radius: Theme.r18

                    Row {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        IconImage {
                            name: "info"
                            size: 16
                            tint: Theme.textSub
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: parent.width - 40
                            text: "所有工作台共用同一套设备端口、日志与任务系统；切换模式不会中断正在执行的操作，也不会另开窗口。"
                            color: Theme.textSub
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
