import QtQuick
import QtQuick.Controls.Basic
import XMF

// Qualcomm 9008 (EDL) workbench: Sahara + Firehose. Completely separate from
// Fastboot and Recovery; needs a Firehose programmer/loader (.mbn/.elf/bin).
Item {
    id: page

    signal backRequested()

    readonly property var steps: [
        { title: "进入 9008 (EDL) 模式", desc: "adb reboot edl / fastboot oem edl，或关机后短接/按键进入 9008",
          tool: "adb.reboot" },
        { title: "Sahara 握手", desc: "识别 9008 端口并完成 Sahara 协议握手", tool: "qcom.sahara.handshake" },
        { title: "上传 Firehose programmer", desc: "选择 .mbn / .elf / .bin Loader 并上传到设备", tool: "qcom.sahara.upload" },
        { title: "Firehose 存储信息", desc: "读取存储类型、扇区大小与容量，确认通信正常", tool: "qcom.firehose.info" },
        { title: "读取 / 写入 / 擦除扇区", desc: "按物理扇区读写或擦除分区（高级操作，务必确认偏移）", tool: "qcom.firehose.program" },
        { title: "从引导包解析 Loader", desc: "按机型从高通引导包中解析出对应的 Loader 文件", tool: "packets.resolve" }
    ]

    function edlPorts() {
        let out = []
        for (let i = 0; i < App.flashPorts.length; ++i) {
            const p = App.flashPorts[i]
            const text = (String(p.kind || "") + " " + String(p.description || "") + " "
                          + String(p.chipLabel || "") + " " + String(p.mode || "")).toLowerCase()
            if (text.indexOf("9008") >= 0 || text.indexOf("edl") >= 0
                    || text.indexOf("sahara") >= 0 || text.indexOf("qualcomm") >= 0)
                out.push(p)
        }
        return out
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 12

        PageHeader {
            width: parent.width
            title: "Qualcomm 9008 深度刷机"
            subtitle: "Sahara + Firehose · 与 Fastboot / Recovery 完全不同的紧急下载模式"
            icon: "cpu"

            AppButton {
                text: "返回"
                icon: "arrow-left"
                kind: 2
                small: true
                onClicked: page.backRequested()
            }
            AppButton {
                text: "刷新端口"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: App.scanPorts()
            }
        }

        GuideFilePanel {
            width: parent.width
            platform: "qcom"
            onUseRequested: function(path, attribute) {
                if (path.length > 0)
                    toolSheet.open(App.toolInfo("qcom.sahara.upload"), { "programmer": path })
            }
        }

        Row {
            width: parent.width
            height: 150
            spacing: 12

            GlassSurface {
                width: parent.width * 0.5
                height: parent.height
                title: "9008 端口状态"

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5

                    Text {
                        width: parent.width
                        visible: page.edlPorts().length === 0
                        text: "未检测到 9008 端口。\n请让设备进入 EDL：adb reboot edl / fastboot oem edl，或关机后按要求短接进入 9008。"
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: page.edlPorts()
                        delegate: Row {
                            width: parent.width
                            height: 22
                            spacing: 10
                            Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.success; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: (modelData.description || "") + "  " + (modelData.portName || "")
                                color: Theme.text
                                font.pixelSize: 11
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        text: "提示：端口名通常显示为 Qualcomm HS-USB QDLoader 9008，VID 05C6。"
                    }
                }
            }

            GlassSurface {
                width: parent.width - parent.width * 0.5 - 12
                height: parent.height
                title: "风险提示"

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5
                    Text {
                        width: parent.width
                        color: Theme.textSub
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        text: "• 9008 模式绕过 Bootloader，可直接读写存储；错误的 Loader 或扇区偏移可能导致设备变砖。\n"
                              + "• 写入前请先用 Firehose 读取并备份目标扇区。\n"
                              + "• 请保证供电稳定；低电量应先充电。"
                    }
                    Text {
                        width: parent.width
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        text: "实现参考：bkerler/edl（Sahara/Firehose 协议）、高通官方 QSaharaServer/fh_loader（内嵌后端）。"
                    }
                }
            }
        }

        GlassSurface {
            width: parent.width
            height: parent.height - 414
            title: "9008 操作步骤"

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                Repeater {
                    model: page.steps
                    delegate: GlassSurface {
                        width: parent.width
                        height: 38
                        radius: Theme.r10
                        color: stepHover.containsMouse ? Theme.glassHover : Theme.glass
                        interactive: true

                        Item {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12

                            Text {
                                id: stepIndex
                                text: (index + 1) + "."
                                color: Theme.textMuted
                                font.pixelSize: 11
                                font.family: "Consolas"
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                id: stepTitle
                                text: modelData.title
                                color: Theme.text
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                anchors.left: stepIndex.right
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.desc
                                color: Theme.textMuted
                                font.pixelSize: 10
                                anchors.left: stepTitle.right
                                anchors.leftMargin: 12
                                anchors.right: stepButton.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                            }
                            AppButton {
                                id: stepButton
                                text: "执行"
                                kind: 1
                                small: true
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: {
                                    if (App.guideFile.length > 0
                                            && (modelData.tool === "qcom.sahara.upload"
                                                || modelData.tool.indexOf("qcom.firehose") === 0))
                                        toolSheet.open(App.toolInfo(modelData.tool), { "programmer": App.guideFile })
                                    else
                                        toolSheet.open(App.toolInfo(modelData.tool))
                                }
                            }
                        }
                        MouseArea {
                            id: stepHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }
                }

                Text {
                    width: parent.width
                    color: Theme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    text: "Firehose 读写工具需要输入物理扇区（sector）而非字节偏移；"
                          + "扇区大小通常为 512/4096，请先用「Firehose 存储信息」确认。"
                }
            }
        }
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }
}
