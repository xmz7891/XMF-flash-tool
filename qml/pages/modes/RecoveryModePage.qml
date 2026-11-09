import QtQuick
import QtQuick.Controls.Basic
import XMF

// Recovery / sideload workbench. Recovery is neither Fastboot nor 9008:
// card/OTA zips are installed through `adb sideload` while the device runs
// a recovery image.
Item {
    id: page

    signal backRequested()

    property string platform: "qcom"
    readonly property var platformNames: ({
        qcom: "Qualcomm 高通",
        mtk: "MediaTek 联发科",
        sprd: "紫光展锐 Unisoc",
        rk: "Rockchip 瑞芯微",
        aw: "Allwinner 全志",
        "": "通用"
    })

    function sideloadDevice() {
        for (let i = 0; i < App.adbDevices.length; ++i) {
            const d = App.adbDevices[i]
            if (String(d.state || "") === "sideload")
                return d
        }
        return null
    }

    function refreshState() {
        App.refreshDevices()
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 12

        PageHeader {
            width: parent.width
            title: "Recovery 模式卡刷"
            subtitle: (page.platformNames[page.platform] || page.platform)
                      + " · 通过 Recovery + ADB Sideload 刷入 zip；这不是 Fastboot，也不是 9008(EDL)"
            icon: "rotate-ccw"

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
                onClicked: page.refreshState()
            }
        }

        Row {
            width: parent.width
            height: 190
            spacing: 12

            GlassSurface {
                width: parent.width * 0.5
                height: parent.height
                title: "ADB / Sideload 设备状态"

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        width: parent.width
                        visible: App.adbDevices.length === 0
                        text: "未检测到 ADB 设备。\n请先进入 Recovery（关机后按住 音量+ + 电源），\n并在 Recovery 中开启 ADB / Sideload。"
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    ListView {
                        width: parent.width
                        height: parent.height - 8
                        visible: App.adbDevices.length > 0
                        model: App.adbDevices
                        spacing: 4
                        clip: true
                        delegate: Row {
                            width: ListView.view.width
                            height: 26
                            spacing: 10
                            Rectangle {
                                width: 7; height: 7; radius: 3.5
                                anchors.verticalCenter: parent.verticalCenter
                                color: String(modelData.state || "") === "sideload"
                                       ? Theme.success
                                       : (String(modelData.state || "") === "device" ? Theme.warning : Theme.danger)
                            }
                            Text {
                                text: modelData.serial || ""
                                color: Theme.text
                                font.pixelSize: 11
                                font.family: "Consolas"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.state || ""
                                color: String(modelData.state || "") === "sideload" ? Theme.success : Theme.textMuted
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                visible: String(modelData.state || "") === "sideload"
                                text: "← 可以开始卡刷"
                                color: Theme.success
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            GlassSurface {
                width: parent.width - parent.width * 0.5 - 12
                height: parent.height
                title: "进入 Recovery 的方式"

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    Text {
                        width: parent.width
                        color: Theme.textSub
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        text: "• 系统可启动：`adb reboot recovery`\n"
                              + "• 系统无法启动：关机后按住 音量+ + 电源 进入 Recovery\n"
                              + "• 第三方 Recovery（TWRP 等）：进入后选择 Apply update → ADB Sideload\n"
                              + "• 官方 Recovery：部分机型支持 ADB Sideload，小米/OPPO 等可能只允许官方 OTA 包"
                    }
                    Text {
                        width: parent.width
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        text: "注意：Recovery 卡刷 ≠ Fastboot 刷分区；A/B 设备 sideload 会走 update_engine 写入非活动槽。"
                    }
                }
            }
        }

        GlassSurface {
            width: parent.width
            height: parent.height - 240
            title: "卡刷步骤"

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Row {
                    width: parent.width
                    height: 28
                    spacing: 8
                    AppButton {
                        text: "重启到 Recovery"
                        icon: "rotate-ccw"
                        kind: 1
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("adb.reboot"))
                    }
                    AppButton {
                        text: "等待设备上线"
                        icon: "clock"
                        kind: 1
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("adb.waitDevice"))
                    }
                    AppButton {
                        text: "选择 zip 并 Sideload"
                        icon: "package-open"
                        kind: 0
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("adb.sideload"))
                    }
                    AppButton {
                        text: "推送 zip 到设备"
                        icon: "upload"
                        kind: 2
                        small: true
                        onClicked: toolSheet.open(App.toolInfo("adb.push"))
                    }
                }

                Rectangle { width: parent.width; height: 1; color: Theme.divider }

                Text {
                    width: parent.width
                    color: Theme.textSub
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    text: "1. 用「重启到 Recovery」把设备送入 Recovery（或手动按键进入）。\n"
                          + "2. 在 Recovery 中开启 ADB Sideload（TWRP：Apply update → ADB Sideload；官方 Recovery：Apply update from ADB）。\n"
                          + "3. 点「等待设备上线」，确认设备状态显示 sideload。\n"
                          + "4. 选择 OTA / 卡刷 zip，执行 Sideload，过程中不要断开数据线。\n"
                          + "5. 完成后在 Recovery 中重启系统。"
                }

                Text {
                    width: parent.width
                    color: Theme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    text: "参考开源项目：PixelFlasher（ADB/fastboot 封装）、OpenAndroidInstaller（Recovery 卡刷流程）。"
                }
            }
        }
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }
}
