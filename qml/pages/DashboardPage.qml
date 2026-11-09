import QtQuick
import QtQuick.Controls.Basic
import XMF

// Overview: device/port state, runtime environment, resource packs and recent
// activity. Anchored layout keeps the panels stable at any window size.
Item {
    id: page

    signal navigateTo(int index)

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 4
        title: "概览"
        subtitle: App.deviceInfo.connected
                  ? ((App.deviceInfo.mode === "fastboot" ? "Fastboot 模式 · " : "ADB 模式 · ")
                     + (App.deviceInfo.model || App.deviceInfo.serial || "设备"))
                  : "未连接设备 · 端口自动检测"
        icon: "home"
        AppButton {
            text: "刷新设备"
            kind: 1
            small: true
            onClicked: { App.scanPorts(); App.refreshDevices() }
        }
        AppButton {
            text: "读取分区"
            kind: 1
            small: true
            onClicked: page.navigateTo(1)
        }
    }

    // ------------------------------------------------------------ row 1
    GlassSurface {
        id: devicePanel
        anchors.top: header.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: envPanel.left
        anchors.rightMargin: 8
        height: 210
        title: "设备与端口"

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2

            Row {
                width: parent.width
                height: 20
                Text { width: 76; text: "端口"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { width: parent.width - 76 - 260; text: "设备描述"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { width: 160; text: "模式"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { width: 60; text: "状态"; color: Theme.textMuted; font.pixelSize: 10 }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            ListView {
                width: parent.width
                height: parent.height - 24
                clip: true
                model: App.flashPorts
                spacing: 0
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Item {
                    width: ListView.view.width
                    height: 26
                    Rectangle { anchors.fill: parent; color: pMouse.containsMouse ? Theme.panelHover : "transparent" }
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        Text { width: 76; text: modelData.portName || "—"; color: Theme.textSub; font.pixelSize: 11; elide: Text.ElideRight }
                        Text { width: parent.width - 76 - 260; text: modelData.description || ""; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                        Text { width: 160; text: modelData.chipLabel || modelData.kindLabel || ""; color: Theme.textSub; font.pixelSize: 11; elide: Text.ElideRight }
                        Text {
                            width: 60
                            text: modelData.hasProblem ? "异常" : "正常"
                            color: modelData.hasProblem ? Theme.danger : Theme.success
                            font.pixelSize: 11
                        }
                    }
                    MouseArea { id: pMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }
                EmptyState {
                    anchors.fill: parent
                    visible: App.flashPorts.length === 0
                    icon: "usb"
                    title: "没有检测到刷机端口"
                    text: "连接设备，或让手机进入 9008 / BROM / FDL / FEL 模式。"
                }
            }
        }
    }

    GlassSurface {
        id: envPanel
        anchors.top: header.bottom
        anchors.topMargin: 6
        anchors.right: parent.right
        width: 300
        height: 210
        title: "运行环境"

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Repeater {
                model: [
                    { k: "ADB 设备", v: App.adbDevices.length + " 台" },
                    { k: "Fastboot 设备", v: App.fastbootDevices.length + " 台" },
                    { k: "驱动状态", v: App.driverSummaryText() },
                    { k: "管理员权限", v: App.admin ? "已获得" : "普通权限" },
                    { k: "工具数量", v: App.tools.length + " 个" },
                    { k: "引导包", v: App.packets.packets.length + " 个平台 / " + App.packets.modelCount + " 个机型" }
                ]
                delegate: Row {
                    width: parent.width
                    Text { width: 84; text: modelData.k; color: Theme.textMuted; font.pixelSize: 11 }
                    Text { width: parent.width - 84; text: modelData.v; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                }
            }

            Item { width: 1; height: 2 }

            Row {
                width: parent.width
                visible: (App.deviceInfo.battery || "").length > 0
                Text { width: 84; text: "电池"; color: Theme.textMuted; font.pixelSize: 11 }
                Text {
                    width: parent.width - 84
                    text: App.deviceInfo.battery + " mV"
                          + (App.deviceInfo.batteryOk === false ? "（低电量/状态异常）" : "")
                    color: App.deviceInfo.batteryOk === false ? Theme.danger : Theme.text
                    font.pixelSize: 11
                }
            }
            Row {
                width: parent.width
                visible: (App.deviceInfo.slot || "").length > 0
                Text { width: 84; text: "当前槽位"; color: Theme.textMuted; font.pixelSize: 11 }
                Text {
                    width: parent.width - 84
                    text: App.deviceInfo.slot + " · BL " + (App.deviceInfo.bootloader || "—")
                    color: Theme.text
                    font.pixelSize: 11
                }
            }

            Row {
                spacing: 6
                AppButton { text: "安装驱动"; kind: 1; small: true; onClicked: App.installDrivers() }
                AppButton { text: "日志"; kind: 1; small: true; onClicked: page.navigateTo(7) }
                AppButton { text: "设置"; kind: 1; small: true; onClicked: page.navigateTo(9) }
            }
        }
    }

    // ------------------------------------------------------------ row 2
    GlassSurface {
        id: packsPanel
        anchors.top: devicePanel.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: logPanel.left
        anchors.rightMargin: 8
        anchors.bottom: quickRow.top
        anchors.bottomMargin: 8
        title: "深度刷机引导"

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2

            Row {
                width: parent.width
                height: 20
                Text { width: 200; text: "名称"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { width: 60; text: "类型"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { width: 70; text: "机型"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { width: 60; text: "品牌"; color: Theme.textMuted; font.pixelSize: 10 }
                Text { text: "来源"; color: Theme.textMuted; font.pixelSize: 10 }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            ListView {
                width: parent.width
                height: parent.height - 24
                clip: true
                model: App.packets.packets
                delegate: Item {
                    width: ListView.view.width
                    height: 26
                    Rectangle { anchors.fill: parent; color: pkMouse.containsMouse ? Theme.panelHover : "transparent" }
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        Text { width: 200; text: modelData.name; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                        Text { width: 60; text: modelData.type; color: Theme.textSub; font.pixelSize: 11 }
                        Text { width: 70; text: modelData.modelCount; color: Theme.text; font.pixelSize: 11 }
                        Text { width: 60; text: modelData.brandCount; color: Theme.textSub; font.pixelSize: 11 }
                        Text { text: modelData.sourceType === "zip" ? "ZIP" : "目录"; color: Theme.textMuted; font.pixelSize: 11 }
                    }
                    MouseArea { id: pkMouse; anchors.fill: parent; hoverEnabled: true; onClicked: page.navigateTo(4) }
                }
                EmptyState {
                    anchors.fill: parent
                    visible: App.packets.packets.length === 0
                    icon: "package"
                    title: "尚未导入引导包"
                    text: "放入 data/packets 或点击「导入固件」。"
                }
            }
        }
    }

    GlassSurface {
        id: logPanel
        anchors.top: devicePanel.bottom
        anchors.topMargin: 8
        anchors.right: parent.right
        width: 430
        anchors.bottom: quickRow.top
        anchors.bottomMargin: 8
        title: "最近日志"

        ListView {
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            model: App.logs
            verticalLayoutDirection: ListView.BottomToTop
            delegate: Row {
                width: ListView.view.width
                height: 17
                spacing: 6
                Text { text: model.time; color: Theme.textMuted; font.pixelSize: 9; font.family: "Consolas"; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    width: parent.width - 82
                    text: model.message
                    color: model.level >= 3 ? (model.level === 3 ? Theme.warning : Theme.danger) : Theme.textSub
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // ------------------------------------------------------------ quick row
    Row {
        id: quickRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 28
        spacing: 6

        Repeater {
            model: [
                { t: "ADB 命令", i: "terminal", p: 10 },
                { t: "Fastboot", i: "zap", p: 1 },
                { t: "镜像工具", i: "disc-3", p: 2 },
                { t: "内置模式", i: "layout-dashboard", p: 3 },
                { t: "引导包", i: "package", p: 4 },
                { t: "官方工具", i: "box", p: 8 }
            ]
            delegate: AppButton {
                text: modelData.t
                icon: modelData.i
                kind: 1
                onClicked: page.navigateTo(modelData.p)
            }
        }
    }
}
