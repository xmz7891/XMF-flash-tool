import QtQuick
import QtQuick.Controls.Basic
import XMF

// Generic Fastboot workbench. Fastboot is a distinct protocol from 9008/EDL and
// from Recovery/sideload: this page only handles fastboot-mode devices.
Item {
    id: page

    signal backRequested()

    property string platform: "qcom"
    property var partitions: []
    property string tableSubtitle: ""
    property var lastResult: null

    readonly property var platformNames: ({
        qcom: "Qualcomm 高通",
        mtk: "MediaTek 联发科",
        sprd: "紫光展锐 Unisoc",
        rk: "Rockchip 瑞芯微",
        aw: "Allwinner 全志",
        "": "通用"
    })

    function deviceSerial() {
        return App.fastbootDevices.length > 0 ? String(App.fastbootDevices[0].serial || "") : ""
    }

    function loadPartitions() {
        if (App.fastbootDevices.length === 0) {
            App.notify("warning", "没有 Fastboot 设备：请让设备进入 Bootloader/Fastboot 模式（不是 9008，也不是 Recovery）")
            return
        }
        lastResult = App.workspace.loadFastbootPartitions(deviceSerial())
        if (!lastResult.ok) {
            App.notify("error", lastResult.error)
            partitions = []
            return
        }
        partitions = lastResult.partitions || []
        tableSubtitle = lastResult.subtitle || ""
        App.notify("success", "已读取 " + partitions.length + " 个分区")
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 12

        PageHeader {
            width: parent.width
            title: "Fastboot 模式刷写"
            subtitle: (page.platformNames[page.platform] || page.platform)
                      + " · 设备需处于 Fastboot / Bootloader；这不是 9008(EDL)，也不是 Recovery"
            icon: "zap"

            AppButton {
                text: "返回"
                icon: "arrow-left"
                kind: 2
                small: true
                onClicked: page.backRequested()
            }
            AppButton {
                text: "刷新设备"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: App.refreshDevices()
            }
        }

        Row {
            width: parent.width
            height: 150
            spacing: 12

            GlassSurface {
                width: parent.width * 0.55
                height: parent.height
                title: "Fastboot 设备"

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        width: parent.width
                        visible: App.fastbootDevices.length === 0
                        text: "未检测到 Fastboot 设备。\n关机后按住 音量- + 电源（多数机型）进入 Bootloader。"
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    ListView {
                        width: parent.width
                        height: parent.height - 8
                        visible: App.fastbootDevices.length > 0
                        model: App.fastbootDevices
                        spacing: 4
                        clip: true
                        delegate: Row {
                            width: ListView.view.width
                            height: 26
                            spacing: 10
                            Rectangle {
                                width: 7; height: 7; radius: 3.5
                                color: Theme.success
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.serial || ""
                                color: Theme.text
                                font.pixelSize: 11
                                font.family: "Consolas"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "BL " + (App.deviceInfo.bootloader || "未知")
                                      + " · 槽位 " + (App.deviceInfo.slot || "—")
                                      + " · 电量 " + (App.deviceInfo.battery || "—") + " mV"
                                color: App.deviceInfo.batteryOk === false ? Theme.danger : Theme.textMuted
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            GlassSurface {
                width: parent.width - parent.width * 0.55 - 12
                height: parent.height
                title: "Fastboot 与 9008 / Recovery 的区别"

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5
                    Text {
                        width: parent.width
                        color: Theme.textSub
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        text: "• Fastboot：解锁 BL 后由 bootloader 提供的分区刷写协议，刷 boot / recovery / vbmeta 等。\n"
                              + "• 9008(EDL)：高通芯片的紧急下载模式，走 Sahara + Firehose，需要 programmer 文件。\n"
                              + "• Recovery：独立恢复系统，ADB sideload 只能刷卡刷/OTA zip，不是 fastboot。"
                    }
                    Text {
                        width: parent.width
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        text: "未解锁 BL 时 fastboot 会拒绝刷写；大包官方固件请用 MiFlash 工作台。"
                    }
                }
            }
        }

        GlassSurface {
            width: parent.width
            height: parent.height - 200

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                Item {
                    width: parent.width
                    height: 28

                    Text {
                        id: partTitle
                        text: "分区表"
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: page.partitions.length > 0
                              ? page.partitions.length + " 个分区 · " + page.tableSubtitle : ""
                        color: Theme.textMuted
                        font.pixelSize: 10
                        anchors.left: partTitle.right
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 300
                        elide: Text.ElideRight
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        AppButton { text: "读取分区"; kind: 1; small: true; onClicked: page.loadPartitions() }
                        AppButton {
                            text: "导出 CSV"
                            kind: 1
                            small: true
                            onClicked: toolSheet.open(App.toolInfo("fastboot.partitions.export"))
                        }
                        AppButton {
                            text: "刷写前检查"
                            kind: 1
                            small: true
                            onClicked: toolSheet.open(App.toolInfo("fastboot.flash.check"))
                        }
                        AppButton {
                            text: "刷写分区"
                            kind: 0
                            small: true
                            onClicked: toolSheet.open(App.toolInfo("fastboot.flash"))
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: 28
                    spacing: 8
                    AppButton {
                        text: "擦除分区"
                        icon: "trash-2"
                        kind: 2
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("fastboot.erase"))
                    }
                    AppButton {
                        text: "重启 / 切换模式"
                        icon: "rotate-ccw"
                        kind: 2
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("fastboot.reboot"))
                    }
                    AppButton {
                        text: "OEM 命令"
                        icon: "terminal"
                        kind: 2
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("fastboot.oem"))
                    }
                    AppButton {
                        text: "电池状态"
                        icon: "battery-charging"
                        kind: 2
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("fastboot.battery"))
                    }
                }

                Rectangle { width: parent.width; height: 1; color: Theme.divider }

                Row {
                    width: parent.width
                    height: 20
                    Text { width: parent.width * 0.34; text: "名称"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: parent.width * 0.22; text: "大小"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: parent.width * 0.22; text: "地址"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: parent.width * 0.22; text: "类型"; color: Theme.textMuted; font.pixelSize: 10 }
                }

                ListView {
                    width: parent.width
                    height: parent.height - 96
                    clip: true
                    model: page.partitions
                    spacing: 2
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        width: ListView.view.width
                        height: 26
                        Row {
                            anchors.fill: parent
                            Text {
                                width: parent.width * 0.34
                                text: modelData.name || ""
                                color: Theme.text
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                width: parent.width * 0.22
                                text: modelData.sizeText || "—"
                                color: Theme.textSub
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                width: parent.width * 0.22
                                text: modelData.address || "—"
                                color: Theme.textMuted
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                width: parent.width * 0.22
                                text: modelData.region || "raw"
                                color: Theme.textMuted
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }

                    EmptyState {
                        anchors.fill: parent
                        visible: page.partitions.length === 0
                        icon: "hard-drive"
                        title: "尚未读取分区"
                        text: "连接 Fastboot 设备后点「读取分区」；未解锁 BL 也可读取分区表。"
                    }
                }
            }
        }
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }
}
