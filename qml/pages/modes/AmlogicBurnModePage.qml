import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import XMF

// Amlogic USB Burning / WorldCup workbench.
// USB IDs: 1B8E:C003 (WorldCup) and 1B8E:C004 (ADNL).
Item {
    id: page
    signal backRequested()

    readonly property color modeColor: Theme.accent
    property string imagePath: ""

    function chooseImage(path) {
        imagePath = path
        frame.fwPath = path
        frame.fwLoaded = path.length > 0
        if (path.length > 0) {
            frame.statusText = "已选择固件，等待设备"
            frame.statusKind = "ready"
            toolSheet.open(App.toolInfo("amlogic.image.info"), { "input": path })
        }
    }
    readonly property var amlPorts: {
        let out = []
        for (let i = 0; i < App.flashPorts.length; ++i) {
            const p = App.flashPorts[i]
            const text = (String(p.kind || "") + " " + String(p.description || "") + " "
                          + String(p.vidPid || "")).toLowerCase()
            if (text.indexOf("amlogic") >= 0 || text.indexOf("1b8e") >= 0
                    || text.indexOf("worldcup") >= 0 || text.indexOf("adnl") >= 0)
                out.push(p)
        }
        return out
    }

    ModeFrame {
        id: frame
        anchors.fill: parent
        title: "Amlogic USB Burning"
        subtitle: "晶晨 WorldCup / ADNL USB 烧录"
        icon: "cpu"
        tag: "AML"
        accentColor: page.modeColor
        firmwareLabel: "固件"
        firmwarePlaceholder: "选择 Amlogic USB Burning 固件（.img）"
        fwPath: ""
        fwLoaded: false
        progress: 0
        statusText: page.amlPorts.length > 0 ? "已检测到 Amlogic USB Burn 端口" : "未检测到 Amlogic USB Burn 端口"
        statusKind: page.amlPorts.length > 0 ? "ready" : "idle"
        deviceText: page.amlPorts.length > 0 ? "已连接" : "未连接"
        deviceKind: page.amlPorts.length > 0 ? "ok" : "idle"
        speedText: "—"
        deviceCode: page.amlPorts.length > 0 ? String(page.amlPorts[0].vidPid || page.amlPorts[0].portName || "—") : "—"
        bootloaderText: "USB Burning"
        slotText: "—"
        onBackRequested: page.backRequested()
        onBrowseRequested: imageDialog.open()
        onLoadRequested: page.chooseImage(frame.fwPath)
        onReloadRequested: { App.scanPorts(); App.refreshDevices() }
        headerActions: [
            AppButton {
                text: "刷新端口"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: { App.scanPorts(); App.refreshDevices() }
            },
            AppButton {
                text: "开始烧写"
                icon: "flame"
                kind: 0
                small: true
                onClicked: {
                    if (page.imagePath.length === 0) {
                        imageDialog.open()
                        return
                    }
                    toolSheet.open(App.toolInfo("amlogic.update"), { "img": page.imagePath })
                }
            },
            AppButton {
                text: "解析烧录包"
                icon: "file-search"
                kind: 1
                small: true
                onClicked: toolSheet.open(App.toolInfo("amlogic.image.info"))
            },
            AppButton {
                text: "Zadig 驱动"
                icon: "external-link"
                kind: 2
                small: true
                onClicked: App.openPath(App.dataPath + "/thirdparty/amlogic/zadig.exe")
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: 12

            ModeCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "USB Burning 设备"
                subtitle: page.amlPorts.length > 0
                          ? "WorldCup / ADNL 端口已就绪"
                          : "请让设备进入 USB Burning 模式"
                icon: "usb"
                accentColor: page.modeColor

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Repeater {
                        model: page.amlPorts
                        delegate: GlassSurface {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            radius: Theme.r10
                            color: Theme.glass
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: (modelData.description || "Amlogic USB Burn") + "\n"
                                      + (modelData.vidPid || "") + "  " + (modelData.portName || "")
                                color: Theme.text
                                font.pixelSize: Theme.fs(10)
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Column {
                        visible: page.amlPorts.length === 0
                        width: parent.width
                        spacing: 6
                        Text {
                            width: parent.width
                            text: "未检测到 Amlogic USB Burning 设备。\n\n"
                                  + "1. 断开设备电源，按住复位/烧录键再上电进入 USB Burning 模式。\n"
                                  + "2. 设备应显示为 1B8E:C003（WorldCup）或 1B8E:C004（ADNL）。\n"
                                  + "3. 如果没有端口，请安装 WorldCup USB 驱动（可用 Zadig 绑定 WinUSB）。"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fs(11)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Text {
                        Layout.fillWidth: true
                        text: "Amlogic USB Burning 后端已接入。\n"
                              + "先用 Zadig 将 1B8E:C003 绑定 libusb0，再选择固件开始烧写。"
                        color: Theme.success
                        font.pixelSize: Theme.fs(10)
                        wrapMode: Text.Wrap
                    }
                }
            }

            ModeCard {
                Layout.preferredWidth: Math.max(300, Math.min(430, page.width * 0.27))
                Layout.fillHeight: true
                title: "USB Burning 状态"
                icon: "shield-check"
                accentColor: page.modeColor

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    ModeStatusLamp {
                        Layout.fillWidth: true
                        stateName: page.amlPorts.length > 0 ? "ok" : "idle"
                        label: "WorldCup / ADNL"
                        detail: page.amlPorts.length > 0 ? "已连接" : "未连接"
                    }
                    ModeStatusLamp {
                        Layout.fillWidth: true
                        stateName: page.imagePath.length > 0 ? "ok" : "idle"
                        label: "固件"
                        detail: page.imagePath.length > 0
                                ? (page.imagePath.split("\\").pop())
                                : "未选择"
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        Layout.fillWidth: true
                        text: "Amlogic 平台 · VID 1B8E\nWorldCup C003 / ADNL C004"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fs(10)
                    }
                }
            }
        }
    }

    FileDialog {
        id: imageDialog
        title: "选择 Amlogic USB Burning 固件"
        nameFilters: ["Amlogic 固件 (*.img)", "所有文件 (*.*)"]
        onAccepted: page.chooseImage(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }
}
