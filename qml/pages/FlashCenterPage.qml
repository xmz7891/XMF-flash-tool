import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import XMF

// Flash center, instrument style:
// mode strip -> platform rail -> mode panel -> device ports + partition table.
Item {
    id: page

    signal openFlashMode(string modeId)

    readonly property var platforms: [
        { id: "qcom", name: "Qualcomm 高通", sub: "骁龙 / 小米 / 一加 / OPPO", icon: "cpu",
          modes: [
              { key: "fastboot:qcom", title: "Fastboot 模式", desc: "已解锁 BL，刷 boot / recovery / vbmeta" },
              { key: "recovery:qcom", title: "Recovery 模式", desc: "ADB sideload 卡刷 OTA / 第三方 zip" },
              { key: "qcom-edl", title: "9008 EDL 模式", desc: "Sahara + Firehose 深度刷写 / 救砖" }
          ] },
        { id: "mtk", name: "MediaTek 联发科", sub: "天玑 / 曦力 / 红米 / 真我", icon: "microchip",
          modes: [
              { key: "fastboot:mtk", title: "Fastboot 模式", desc: "刷 boot / recovery / 分区镜像" },
              { key: "recovery:mtk", title: "Recovery 模式", desc: "ADB sideload 卡刷 zip" },
              { key: "spflash", title: "BROM / DA 模式", desc: "Preloader + DA 深度刷机（SP Flash）" }
          ] },
        { id: "sprd", name: "Unisoc 紫光展锐", sub: "虎贲 / 唐古拉", icon: "layers",
          modes: [
              { key: "fastboot:sprd", title: "Fastboot 模式", desc: "刷 boot / recovery / 分区镜像" },
              { key: "recovery:sprd", title: "Recovery 模式", desc: "ADB sideload 卡刷 zip" },
              { key: "spdflash", title: "U2S / FDL 模式", desc: "FDL1 + FDL2 深度刷机（Research Download）" }
          ] },
        { id: "rk", name: "Rockchip 瑞芯微", sub: "RK33xx / RK35xx", icon: "box",
          modes: [
              { key: "fastboot:rk", title: "Fastboot 模式", desc: "刷 boot / recovery / 分区镜像" },
              { key: "recovery:rk", title: "Recovery 模式", desc: "ADB sideload 卡刷 zip" },
              { key: "rkdevtool", title: "MaskROM / Loader", desc: "MaskROM/Loader 深度刷写（RKDevTool）" }
          ] },
        { id: "aw", name: "Allwinner 全志", sub: "A 系列 / 平板 / 盒子", icon: "sun",
          modes: [
              { key: "fastboot:aw", title: "Fastboot 模式", desc: "刷 boot / recovery / 分区镜像" },
              { key: "recovery:aw", title: "Recovery 模式", desc: "ADB sideload 卡刷 zip" },
              { key: "phoenixsuit", title: "FEL 模式", desc: "FEL 深度刷机（PhoenixSuit）" }
          ] }
    ]

    property int selectedPlatform: 0
    readonly property var currentPlatform: platforms[Math.max(0, Math.min(selectedPlatform, platforms.length - 1))]

    property var partitionResult: null
    property string partitionSource: ""
    property bool autoSwitch: !XMF_NO_AUTO_SWITCH && App.settings.get("ui/autoSwitchMode", true)
    property string lastAutoKey: ""

    function detectedKey() {
        return String(App.deviceMode.mode || "") + ":" + String(App.deviceMode.platform || "")
    }

    function detectedPlatform() {
        return String(App.deviceMode.platform || "qcom")
    }

    function openDetectedMode() {
        const mode = String(App.deviceMode.mode || "")
        const platform = page.detectedPlatform()
        if (mode === "fastboot")
            page.openFlashMode("fastboot:" + platform)
        else if (mode === "sideload")
            page.openFlashMode("recovery:" + platform)
        else if (mode === "edl9008")
            page.openFlashMode("qcom-edl")
        else if (mode === "mtk-brom")
            page.openFlashMode("spflash")
        else if (mode === "sprd-fdl")
            page.openFlashMode("spdflash")
        else if (mode === "rk-maskrom")
            page.openFlashMode("rkdevtool")
        else if (mode === "aw-fel")
            page.openFlashMode("phoenixsuit")
        else
            App.notify("info", "当前没有检测到可刷机的设备模式，请手动选择左侧平台")
    }

    function maybeAutoSwitch() {
        if (!page.autoSwitch || !page.visible)
            return
        const mode = String(App.deviceMode.mode || "")
        const actionable = ["fastboot", "sideload", "edl9008", "mtk-brom",
                            "sprd-fdl", "rk-maskrom", "aw-fel"]
        if (actionable.indexOf(mode) < 0)
            return
        const key = page.detectedKey()
        if (key === page.lastAutoKey)
            return
        page.lastAutoKey = key
        Qt.callLater(function() {
            if (page.visible)
                page.openDetectedMode()
        })
    }

    onVisibleChanged: page.maybeAutoSwitch()
    Component.onCompleted: page.maybeAutoSwitch()

    Connections {
        target: App
        function onDeviceModeChanged() { page.maybeAutoSwitch() }
    }

    function loadDevicePartitions() {
        if (App.adbDevices.length === 0 && App.fastbootDevices.length === 0) {
            App.notify("warning", "未连接 ADB 或 Fastboot 设备，请连接后再读取分区表")
            return
        }
        if (App.adbDevices.length > 0) {
            partitionResult = App.workspace.loadAdbPartitions(App.adbDevices[0].serial)
            partitionSource = "设备 " + (App.adbDevices[0].serial || "")
        } else {
            partitionResult = App.workspace.loadFastbootPartitions(App.fastbootDevices[0].serial)
            partitionSource = "Fastboot " + (App.fastbootDevices[0].serial || "")
        }
        if (!partitionResult.ok)
            App.notify("error", partitionResult.error)
        else
            App.notify("success", "已读取 " + partitionResult.partitions.length + " 个分区")
    }

    function applyFirmwareResult(result, source) {
        if (!result || !result.ok) {
            App.notify("error", result && result.error ? result.error : "固件解析失败")
            return false
        }
        partitionResult = result
        partitionSource = source
        App.notify("success", "已解析 " + (result.partitions || []).length + " 个分区：" + (result.subtitle || ""))
        return true
    }

    function loadAnyFirmware(path) {
        const lower = path.toLowerCase()
        let result = null
        if (lower.endsWith(".tgz") || lower.endsWith(".tar.gz"))
            result = App.workspace.loadMiFlashPackage(path)
        else if (lower.endsWith(".pac"))
            result = App.workspace.loadPac(path)
        else if (lower.endsWith(".txt") || lower.endsWith(".csv"))
            result = App.workspace.loadScatter(path)
        else {
            result = App.workspace.loadRkFirmware(path)
            if (!result.ok)
                result = App.workspace.loadAwFirmware(path)
            if (!result.ok)
                result = App.workspace.loadGptImage(path)
        }
        return applyFirmwareResult(result, path)
    }

    function loadImagePartitions(path) {
        partitionResult = App.workspace.loadGptImage(path)
        partitionSource = path
        if (!partitionResult.ok)
            App.notify("error", partitionResult.error)
        else
            App.notify("success", "已解析 " + partitionResult.partitions.length + " 个分区")
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 8
        spacing: 10

        PageHeader {
            width: parent.width
            title: "刷机中心"
            subtitle: "先确认设备模式：Fastboot / Recovery / 9008(EDL) 是三种不同的刷机协议"
            icon: "zap"

            AppButton {
                text: "读取分区"
                icon: "hard-drive"
                kind: 1
                small: true
                onClicked: page.loadDevicePartitions()
            }
            AppButton {
                text: "导出分区表"
                icon: "file-down"
                kind: 1
                small: true
                onClicked: toolSheet.open(App.toolInfo("fastboot.partitions.export"))
            }
            AppButton {
                text: "刷新设备"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: { App.scanPorts(); App.refreshDevices() }
            }
        }

        GlassSurface {
            width: parent.width
            height: 60
            darkHeader: false

            Row {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12

                Rectangle {
                    width: 9; height: 9; radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: (String(App.deviceMode.mode || "") === "none"
                            || String(App.deviceMode.mode || "") === "adb"
                            || String(App.deviceMode.mode || "") === "unauthorized")
                           ? Theme.textMuted : Theme.success
                }
                Column {
                    width: 380
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Row {
                        spacing: 8
                        Text { text: "当前设备"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text {
                            text: App.deviceMode.label || "未检测到设备"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        text: App.deviceMode.detail || "连接设备后自动识别模式"
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                CheckBox {
                    text: "自动切换到对应刷机模式"
                    font.pixelSize: 11
                    checked: page.autoSwitch
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: {
                        page.autoSwitch = checked
                        App.settings.set("ui/autoSwitchMode", checked)
                    }
                }
                Item { width: Math.max(0, parent.width - 830); height: 1 }
                AppButton {
                    text: "打开对应工作台"
                    icon: "arrow-up-right"
                    kind: 0
                    small: true
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: page.openDetectedMode()
                }
                AppButton {
                    text: "刷写前检查"
                    icon: "shield-check"
                    kind: 1
                    small: true
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: toolSheet.open(App.toolInfo("fastboot.flash.check"))
                }
            }
        }

        Row {
            width: parent.width
            height: 270
            spacing: 10

            GlassSurface {
                width: 232
                height: parent.height
                title: "芯片平台"
                hint: "选择后查看模式"

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Repeater {
                        model: page.platforms
                        delegate: Rectangle {
                            width: parent.width
                            height: 40
                            radius: Theme.r6
                            color: page.selectedPlatform === index ? Theme.accentSoft
                                                                   : (platMouse.containsMouse ? Theme.panelHover : "transparent")
                            border.width: 1
                            border.color: page.selectedPlatform === index ? Theme.accent : "transparent"

                            Rectangle {
                                visible: page.selectedPlatform === index
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                color: Theme.accent
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8
                                spacing: 8
                                IconImage {
                                    name: modelData.icon
                                    size: 16
                                    tint: page.selectedPlatform === index ? Theme.accent : Theme.textSub
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Column {
                                    width: parent.width - 54
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 0
                                    Text {
                                        width: parent.width
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.sub
                                        color: Theme.textMuted
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                                IconImage {
                                    name: "chevron-right"
                                    size: 12
                                    tint: Theme.textMuted
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: platMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.selectedPlatform = index
                            }
                        }
                    }
                }
            }

            GlassSurface {
                width: parent.width - 242
                height: parent.height
                title: "刷机模式"
                hint: page.currentPlatform ? page.currentPlatform.name : ""

                Column {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Repeater {
                        model: page.currentPlatform ? page.currentPlatform.modes : []
                        delegate: Rectangle {
                            width: parent.width
                            height: 66
                            radius: Theme.r6
                            color: modeMouse.containsMouse ? Theme.panelHover : Theme.panelAlt
                            border.width: 1
                            border.color: modeMouse.containsMouse ? Theme.accent : Theme.border

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Rectangle {
                                    width: 36; height: 36
                                    radius: Theme.r6
                                    color: Theme.sectionBg
                                    anchors.verticalCenter: parent.verticalCenter
                                    IconImage {
                                        anchors.centerIn: parent
                                        name: modelData.key.indexOf("9008") >= 0 ? "cpu" : "zap"
                                        size: 17
                                        tint: Theme.accent
                                    }
                                }
                                Column {
                                    width: parent.width - 110
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Text {
                                        text: modelData.title
                                        color: Theme.text
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: modelData.desc
                                        color: Theme.textMuted
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                                IconImage {
                                    name: "arrow-up-right"
                                    size: 15
                                    tint: Theme.accent
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: modeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.openFlashMode(modelData.key)
                            }
                        }
                    }
                }
            }
        }

        Row {
            width: parent.width
            height: Math.max(150, page.height - 452)
            spacing: 10

            GlassSurface {
                width: 300
                height: parent.height
                title: "设备端口"
                hint: App.flashPorts.length + " 个"

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    ListView {
                        width: parent.width
                        height: parent.height
                        clip: true
                        model: App.flashPorts
                        spacing: 3
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 44
                            radius: Theme.r6
                            color: portMouse.containsMouse ? Theme.panelHover : Theme.panelAlt
                            border.width: 1
                            border.color: Theme.divider

                            Row {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8
                                Rectangle {
                                    width: 7; height: 7; radius: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: (modelData.kind === "adb" || modelData.kind === "fastboot")
                                           ? Theme.success : Theme.warning
                                }
                                Column {
                                    width: parent.width - 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1
                                    Text {
                                        width: parent.width
                                        text: modelData.description || modelData.portName || "USB device"
                                        color: Theme.text
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: (modelData.portName || "") + " · "
                                              + (modelData.chipLabel || modelData.kindLabel || "")
                                        color: Theme.textMuted
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            MouseArea {
                                id: portMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }

                    EmptyState {
                        anchors.fill: parent
                        visible: App.flashPorts.length === 0
                        icon: "usb"
                        title: "没有检测到设备"
                        text: "连接设备，或让设备进入 Fastboot / 9008 / BROM / FDL / FEL 模式。"
                    }
                }
            }

            GlassSurface {
                width: parent.width - 312
                height: parent.height
                title: "固件 / 分区表"
                hint: page.partitionResult !== null && page.partitionResult.ok
                      ? page.partitionResult.partitions.length + " 个分区 · " + page.partitionSource : ""

                Column {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Row {
                        width: parent.width
                        height: 26
                        spacing: 8
                        AppButton {
                            text: "读取设备分区"
                            kind: 1
                            small: true
                            onClicked: page.loadDevicePartitions()
                        }
                        AppButton {
                            text: "从镜像读取 GPT"
                            kind: 1
                            small: true
                            onClicked: gptDialog.open()
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "也可把固件/镜像拖进本页（tgz / pac / scatter / RKFW / IMAGEWTY / GPT）"
                            color: Theme.textMuted
                            font.pixelSize: 10
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.divider }

                    Row {
                        width: parent.width
                        height: 20
                        Text { width: parent.width * 0.32; text: "名称"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.2; text: "大小"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.24; text: "地址 / 设备节点"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.24; text: "区域 / 文件"; color: Theme.textMuted; font.pixelSize: 10 }
                    }

                    Item {
                        width: parent.width
                        height: parent.height - 60

                        ListView {
                            anchors.fill: parent
                            clip: true
                            visible: page.partitionResult !== null && page.partitionResult.ok
                                     && page.partitionResult.partitions.length > 0
                            model: visible ? page.partitionResult.partitions : []
                            spacing: 0
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Item {
                                width: ListView.view.width
                                height: 28
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Theme.divider
                                }
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 2
                                    Text {
                                        width: parent.width * 0.32
                                        text: modelData.name || ""
                                        color: Theme.text
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: parent.width * 0.2
                                        text: modelData.sizeText || "—"
                                        color: Theme.textSub
                                        font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: parent.width * 0.24
                                        text: modelData.address && modelData.address !== "0x0"
                                              ? modelData.address : (modelData.file || "")
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: parent.width * 0.24
                                        text: modelData.region || modelData.detail || ""
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }

                        EmptyState {
                            anchors.fill: parent
                            visible: page.partitionResult === null || !page.partitionResult.ok
                                     || page.partitionResult.partitions.length === 0
                            icon: "hard-drive"
                            title: page.partitionResult !== null && !page.partitionResult.ok ? "无法读取分区表" : "尚未读取分区表"
                            text: page.partitionResult !== null && !page.partitionResult.ok
                                  ? page.partitionResult.error
                                  : "连接设备后点「读取设备分区」，或拖入固件/镜像解析。"
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: gptDialog
        title: "选择磁盘镜像"
        nameFilters: ["磁盘镜像 (*.img *.bin *.raw)", "所有文件 (*.*)"]
        onAccepted: page.loadImagePartitions(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }

    DropArea {
        anchors.fill: parent
        keys: ["urls"]

        Rectangle {
            anchors.fill: parent
            visible: parent.containsDrag
            color: Theme.alpha(Theme.accent, 0.10)
            border.width: 2
            border.color: Theme.accent
            radius: Theme.r6
            z: 50

            Text {
                anchors.centerIn: parent
                text: "松开以解析固件（tgz / pac / scatter / RKFW / IMAGEWTY / GPT 镜像）"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }

        onDropped: function(drop) {
            if (!drop.hasUrls)
                return
            for (let i = 0; i < drop.urls.length; ++i) {
                const path = String(drop.urls[i]).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
                if (page.loadAnyFirmware(path))
                    break
            }
            drop.accept()
        }
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }
}
