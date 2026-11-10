import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import XMF

// Instrument-style single-window shell:
// caption bar -> command menu bar -> icon rail + flashing console stack -> status bar.
Window {
    id: win
    width: 1180
    height: 760
    minimumWidth: 1040
    minimumHeight: 680
    visible: true
    color: Theme.windowBg
    title: "XMF Flash Tool"
    flags: Qt.Window | Qt.FramelessWindowHint

    readonly property bool maximized: visibility === Window.Maximized
    property bool forceClose: false
    property int navIndex: 0
    property string currentPage: "console"

    Component.onCompleted: {
        if (Theme.animations) {
            opacity = 0
            introFade.start()
        }
        const w = Number(App.settings.get("ui/windowWidth", 1180))
        const h = Number(App.settings.get("ui/windowHeight", 760))
        width = Math.max(minimumWidth, w)
        height = Math.max(minimumHeight, h)
        const x = Number(App.settings.get("ui/windowX", -1))
        const y = Number(App.settings.get("ui/windowY", -1))
        if (x >= 0 && y >= 0) {
            win.x = Math.max(0, Math.min(x, Math.max(0, screen.width - width)))
            win.y = Math.max(0, Math.min(y, Math.max(0, screen.height - height)))
        } else {
            win.x = Math.max(0, (screen.width - width) / 2)
            win.y = Math.max(0, (screen.height - height) / 2)
        }
    }

    onClosing: function(close) {
        if (!maximized) {
            App.settings.set("ui/windowWidth", width)
            App.settings.set("ui/windowHeight", height)
            App.settings.set("ui/windowX", x)
            App.settings.set("ui/windowY", y)
        }
        if (!forceClose && App.activeTasks.length > 0) {
            close.accepted = false
            exitConfirm.visible = true
        }
    }

    // ------------------------------------------------------------ navigation
    function pushPage(comp, props, key) {
        if (comp === null)
            return
        win.currentPage = key
        stack.push(pageHostComp, { "pageComp": comp, "pageProps": props || ({}) })
    }

    function pageComponent(key) {
        switch (key) {
        case "tools": return toolsComp
        case "modes": return modesComp
        case "packets": return packetsComp
        case "firmware": return firmwareComp
        case "batch": return batchComp
        case "logs": return logsComp
        case "official": return officialComp
        case "settings": return settingsComp
        case "cmd-console": return consolePageComp
        }
        return null
    }

    function openPageByKey(key) {
        if (stack.depth > 1) {
            if (win.currentPage === key)
                return
            win.currentPage = key
            stack.replace(pageHostComp, { "pageComp": pageComponent(key), "pageProps": ({}) })
            return
        }
        switch (key) {
        case "tools": pushPage(toolsComp, {}, key); break
        case "modes": pushPage(modesComp, {}, key); break
        case "packets": pushPage(packetsComp, {}, key); break
        case "firmware": pushPage(firmwareComp, {}, key); break
        case "batch": pushPage(batchComp, {}, key); break
        case "logs": pushPage(logsComp, {}, key); break
        case "official": pushPage(officialComp, {}, key); break
        case "settings": pushPage(settingsComp, {}, key); break
        case "cmd-console": pushPage(consolePageComp, {}, key); break
        }
    }

    function navigate(index) {
        navIndex = index
        if (index <= 1) {
            popToConsole()
            if (index === 1)
                Qt.callLater(console.readPartitions)
            return
        }
        const keys = ["console", "console", "tools", "modes", "packets", "firmware",
                      "batch", "logs", "official", "settings", "cmd-console"]
        if (index < keys.length)
            openPageByKey(keys[index])
    }

    function popToConsole() {
        if (stack.depth > 1)
            stack.pop(null)
        win.currentPage = "console"
    }

    function openModePage(modeId) {
        switch (modeId) {
        case "miflash": pushPage(miflashComp, {}, "miflash"); break
        case "spflash": pushPage(spflashComp, {}, "spflash"); break
        case "spdflash": pushPage(spdflashComp, {}, "spdflash"); break
        case "rkdevtool": pushPage(rkdevtoolComp, {}, "rkdevtool"); break
        case "phoenixsuit": pushPage(phoenixsuitComp, {}, "phoenixsuit"); break
        case "fastboot": pushPage(fastbootModeComp, { "platform": "qcom" }, "fastboot"); break
        case "recovery": pushPage(recoveryModeComp, { "platform": "qcom" }, "recovery"); break
        case "qcom-edl": pushPage(qcomEdlComp, {}, "qcom-edl"); break
        }
    }

    function openFlashMode(modeId) {
        const id = String(modeId)
        if (id.indexOf("fastboot:") === 0) {
            pushPage(fastbootModeComp, { "platform": id.substring("fastboot:".length) }, "fastboot")
            return
        }
        if (id.indexOf("recovery:") === 0) {
            pushPage(recoveryModeComp, { "platform": id.substring("recovery:".length) }, "recovery")
            return
        }
        openModePage(id)
    }

    function openEmbeddedTool(toolId) {
        pushPage(embeddedComp, { "toolId": toolId }, "embedded")
    }

    function autoReadDevicePartitions() {
        popToConsole()
        Qt.callLater(console.readPartitions)
    }

    function pageLabel(key) {
        switch (key) {
        case "tools": return "工具库"
        case "modes": return "内置模式"
        case "packets": return "深度刷机引导"
        case "firmware": return "固件资源"
        case "batch": return "批量任务"
        case "logs": return "日志"
        case "official": return "官方工具"
        case "settings": return "设置"
        case "cmd-console": return "命令控制台"
        case "miflash": return "MiFlash 工作台"
        case "spflash": return "SP Flash 工作台"
        case "spdflash": return "SPD Flash 工作台"
        case "rkdevtool": return "RKDevTool 工作台"
        case "phoenixsuit": return "PhoenixSuit 工作台"
        case "fastboot": return "Fastboot 工作台"
        case "recovery": return "Recovery 工作台"
        case "qcom-edl": return "Qualcomm 9008 工作台"
        case "embedded": return "官方工具"
        }
        return key
    }

    function toggleMaximize() {
        if (visibility === Window.Maximized)
            showNormal()
        else
            showMaximized()
    }

    // -------------------------------------------------------------- shell
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: maximized ? 0 : 6
        color: Theme.windowBg
        border.width: 1
        border.color: Theme.windowBorder
        clip: true

        Column {
            anchors.fill: parent

            Item {
                id: titleBar
                width: parent.width
                height: 34

                Rectangle {
                    anchors.fill: parent
                    color: Theme.sectionBg
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: win.startSystemMove()
                    onDoubleClicked: win.toggleMaximize()
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Rectangle {
                        width: 20; height: 20
                        radius: 4
                        clip: true
                        color: "transparent"
                        Image {
                            anchors.fill: parent
                            source: "qrc:/images/xmf_icon.png"
                            fillMode: Image.PreserveAspectCrop
                            smooth: true
                        }
                    }
                    Text {
                        text: "XMF Flash Tool"
                        color: Theme.sectionText
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "v" + App.version
                        color: Theme.sectionMuted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        visible: App.admin
                        width: 44; height: 15
                        radius: 2
                        color: Theme.alpha(Theme.warning, 0.16)
                        Text {
                            anchors.centerIn: parent
                            text: "管理员"
                            color: Theme.warning
                            font.pixelSize: 9
                        }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    WindowButton { kind: 0; implicitHeight: 30; implicitWidth: 42; onClicked: win.showMinimized() }
                    WindowButton { kind: 1; implicitHeight: 30; implicitWidth: 42; onClicked: win.toggleMaximize() }
                    WindowButton { kind: 2; implicitHeight: 30; implicitWidth: 42; onClicked: win.close() }
                }
            }

            Item {
                id: menuBar
                width: parent.width
                height: 38

                Rectangle {
                    anchors.fill: parent
                    color: Theme.sectionBg
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: "#1F1E1A"
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    AppButton { text: "功能 ▾"; kind: 2; small: true; onDark: true; onClicked: funcMenu.popup() }
                    AppButton { text: "通用功能 ▾"; kind: 2; small: true; onDark: true; onClicked: commonMenu.popup() }
                    AppButton { text: "高级功能 ▾"; kind: 2; small: true; onDark: true; onClicked: advancedMenu.popup() }
                    AppButton { text: "其他功能 ▾"; kind: 2; small: true; onDark: true; onClicked: otherMenu.popup() }
                    AppButton { text: "辅助功能 ▾"; kind: 2; small: true; onDark: true; onClicked: helpMenu.popup() }
                }

                Menu {
                    id: funcMenu
                    MenuItem { text: "刷机台"; onTriggered: win.popToConsole() }
                    MenuSeparator { }
                    MenuItem { text: "工具库"; onTriggered: win.openPageByKey("tools") }
                    MenuItem { text: "内置模式"; onTriggered: win.openPageByKey("modes") }
                    MenuItem { text: "深度刷机引导"; onTriggered: win.openPageByKey("packets") }
                    MenuItem { text: "固件资源"; onTriggered: win.openPageByKey("firmware") }
                    MenuItem { text: "批量任务"; onTriggered: win.openPageByKey("batch") }
                    MenuItem { text: "日志"; onTriggered: win.openPageByKey("logs") }
                    MenuItem { text: "官方工具"; onTriggered: win.openPageByKey("official") }
                    MenuItem { text: "命令控制台"; onTriggered: win.openPageByKey("cmd-console") }
                    MenuItem { text: "设置"; onTriggered: win.openPageByKey("settings") }
                }
                Menu {
                    id: commonMenu
                    MenuItem { text: "读取分区表"; onTriggered: { win.popToConsole(); Qt.callLater(console.readPartitions) } }
                    MenuItem { text: "读取设备信息"; onTriggered: { win.popToConsole(); console.openTool("adb.info") } }
                    MenuItem { text: "Fastboot 电池状态"; onTriggered: { win.popToConsole(); console.openTool("fastboot.battery") } }
                    MenuSeparator { }
                    MenuItem { text: "刷写前检查"; onTriggered: { win.popToConsole(); console.openTool("fastboot.flash.check") } }
                    MenuItem { text: "导出分区表 CSV"; onTriggered: { win.popToConsole(); console.openTool("fastboot.partitions.export") } }
                    MenuItem { text: "环境自检"; onTriggered: App.runTool("system.selfTest", ({})) }
                }
                Menu {
                    id: advancedMenu
                    MenuItem { text: "Fastboot 工作台"; onTriggered: win.openFlashMode("fastboot:qcom") }
                    MenuItem { text: "Recovery 卡刷"; onTriggered: win.openFlashMode("recovery:qcom") }
                    MenuItem { text: "Qualcomm 9008 (EDL)"; onTriggered: win.openFlashMode("qcom-edl") }
                    MenuSeparator { }
                    MenuItem { text: "MTK BROM / DA（SP Flash）"; onTriggered: win.openModePage("spflash") }
                    MenuItem { text: "Unisoc U2S / FDL"; onTriggered: win.openModePage("spdflash") }
                    MenuItem { text: "Rockchip MaskROM / Loader"; onTriggered: win.openModePage("rkdevtool") }
                    MenuItem { text: "Allwinner FEL"; onTriggered: win.openModePage("phoenixsuit") }
                }
                Menu {
                    id: otherMenu
                    MenuItem { text: "ADB 命令控制台"; onTriggered: win.openPageByKey("cmd-console") }
                    MenuItem { text: "多设备并行命令"; onTriggered: { win.popToConsole(); console.openTool("adb.multiShell") } }
                    MenuItem { text: "Root 痕迹检测"; onTriggered: { win.popToConsole(); console.openTool("root.hide") } }
                    MenuItem { text: "APatch 内核检查"; onTriggered: { win.popToConsole(); console.openTool("root.apatch.patch") } }
                    MenuItem { text: "小米固件检索"; onTriggered: { win.popToConsole(); console.openTool("firmware.xiaomi.search") } }
                }
                Menu {
                    id: helpMenu
                    MenuItem { text: "打开数据目录"; onTriggered: App.openPath(App.dataPath) }
                    MenuItem { text: "日志目录"; onTriggered: App.openPath(App.dataPath) }
                    MenuItem { text: "使用指南"; onTriggered: App.openDocs("USER_GUIDE.md") }
                    MenuItem { text: "检查更新"; onTriggered: App.openUrl("https://github.com/xmz7891/XMF-flash-tool/releases/latest") }
                    MenuItem { text: "扫描引导包"; onTriggered: App.packets.refresh() }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Text {
                        text: "端口 " + App.flashPorts.length
                        color: Theme.sectionMuted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    AppButton {
                        text: "刷新"
                        icon: "refresh-cw"
                        kind: 1
                        small: true
                        onDark: true
                        onClicked: { App.scanPorts(); App.refreshDevices() }
                    }
                    Rectangle { width: 1; height: 18; color: "#4A463C" }
                    Text {
                        text: App.deviceMode.label || "未检测到设备"
                        color: App.deviceMode.mode === "none" ? Theme.sectionMuted : Theme.sectionText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle { width: 1; height: 18; color: "#4A463C" }
                    Text {
                        text: "存储 自动"
                        color: Theme.sectionMuted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Row {
                width: parent.width
                height: parent.height - 34 - 38 - 26

                Rectangle {
                    width: 54
                    height: parent.height
                    color: Theme.sectionBg

                    Column {
                        anchors.top: parent.top
                        anchors.topMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 2

                        readonly property var items: [
                            { key: "console", icon: "zap", label: "刷机台" },
                            { key: "tools", icon: "disc-3", label: "工具库" },
                            { key: "modes", icon: "layout-dashboard", label: "模式" },
                            { key: "packets", icon: "package", label: "引导" },
                            { key: "firmware", icon: "download-cloud", label: "固件" },
                            { key: "batch", icon: "list-checks", label: "批量" },
                            { key: "logs", icon: "file-text", label: "日志" },
                            { key: "official", icon: "box", label: "官方" },
                            { key: "settings", icon: "settings", label: "设置" }
                        ]

                        Repeater {
                            model: parent.items
                            delegate: Item {
                                width: 46; height: 48
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.r6
                                    color: win.currentPage === modelData.key ? "#3B372E" : (railMouse.containsMouse ? "#332F28" : "transparent")
                                    Behavior on color { ColorAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
                                }
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    IconImage {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        name: modelData.icon
                                        size: 18
                                        tint: win.currentPage === modelData.key ? Theme.accent : "#B9AE9A"
                                        scale: railMouse.containsMouse ? 1.10 : 1.0
                                        Behavior on scale { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
                                        Behavior on tint { ColorAnimation { duration: Theme.durFast } }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.label
                                        color: win.currentPage === modelData.key ? Theme.accent : "#9C9182"
                                        font.pixelSize: 9
                                    }
                                }
                                MouseArea {
                                    id: railMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.key === "console")
                                            win.popToConsole()
                                        else
                                            win.openPageByKey(modelData.key)
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    width: parent.width - 54
                    height: parent.height

                    Column {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            id: overlayBar
                            width: parent.width
                            height: 30
                            color: Theme.panelAlt

                            Row {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                AppButton {
                                    visible: stack.depth > 1
                                    text: "返回刷机台"
                                    icon: "arrow-left"
                                    kind: 2
                                    small: true
                                    onClicked: win.popToConsole()
                                }
                                Rectangle {
                                    visible: stack.depth === 1
                                    width: 16; height: 16
                                    radius: 3
                                    color: Theme.sectionBg
                                    IconImage { anchors.centerIn: parent; name: "zap"; size: 10; tint: Theme.accent }
                                }
                                Text {
                                    id: crumbText
                                    text: stack.depth === 1 ? "刷机台" : ("刷机台 / " + win.pageLabel(win.currentPage))
                                    onTextChanged: crumbFade.restart()
                                    color: stack.depth === 1 ? Theme.text : Theme.textMuted
                                    font.pixelSize: 11
                                    font.weight: stack.depth === 1 ? Font.DemiBold : Font.Normal
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    visible: stack.depth === 1
                                    text: "分区 " + console.rows.length + " · 已选 " + console.selectedRows.length
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            SequentialAnimation {
                                id: crumbFade
                                NumberAnimation { target: crumbText; property: "opacity"; from: 0.25; to: 1; duration: Theme.animations ? 220 : 0; easing.type: Easing.OutCubic }
                            }

                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                Text {
                                    text: App.deviceMode.label || "未连接设备"
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                }
                                Rectangle { width: 1; height: 12; color: Theme.divider }
                                Text {
                                    text: App.activeTasks.length > 0 ? "任务 " + App.activeTasks.length : "就绪"
                                    color: App.activeTasks.length > 0 ? Theme.accent : Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Theme.divider
                            }
                        }

                        StackView {
                            id: stack
                            x: 8
                            width: parent.width - 16
                            height: parent.height - 30
                            pushEnter: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "x"; from: 30; to: 0; duration: Theme.animations ? 260 : 0; easing.type: Easing.OutQuint }
                                    NumberAnimation { property: "scale"; from: 0.996; to: 1; duration: Theme.animations ? 260 : 0; easing.type: Easing.OutQuint }
                                }
                            }
                            pushExit: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "x"; from: 0; to: -12; duration: Theme.animations ? 260 : 0; easing.type: Easing.OutQuint }
                                    NumberAnimation { property: "opacity"; from: 1; to: 0.55; duration: Theme.animations ? 260 : 0; easing.type: Easing.OutCubic }
                                }
                            }
                            popEnter: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "opacity"; from: 0.45; to: 1; duration: Theme.animations ? 170 : 0; easing.type: Easing.OutCubic }
                                    NumberAnimation { property: "x"; from: -16; to: 0; duration: Theme.animations ? 170 : 0; easing.type: Easing.OutCubic }
                                }
                            }
                            popExit: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "x"; from: 0; to: 24; duration: Theme.animations ? 240 : 0; easing.type: Easing.OutQuint }
                                    NumberAnimation { property: "opacity"; from: 1; to: 0.55; duration: Theme.animations ? 240 : 0; easing.type: Easing.OutCubic }
                                }
                            }
                            replaceEnter: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "x"; from: 18; to: 0; duration: Theme.animations ? 230 : 0; easing.type: Easing.OutQuint }
                                }
                            }
                            replaceExit: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "x"; from: 0; to: -8; duration: Theme.animations ? 230 : 0; easing.type: Easing.OutQuint }
                                    NumberAnimation { property: "opacity"; from: 1; to: 0.6; duration: Theme.animations ? 230 : 0; easing.type: Easing.OutCubic }
                                }
                            }
                            initialItem: FlashConsolePage {
                                id: console
                                onOpenMode: function(modeId) { win.openModePage(modeId) }
                                onOpenPage: function(key) { win.openPageByKey(key) }
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: statusBar
                width: parent.width
                height: 26
                color: Theme.panelAlt

                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.divider }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14

                    Text {
                        text: "设备: " + (App.deviceInfo.serial || App.deviceInfo.model || "—")
                        color: Theme.textSub
                        font.pixelSize: 10
                    }
                    Text {
                        text: "BL: " + (App.deviceInfo.bootloader || "—")
                        color: Theme.textSub
                        font.pixelSize: 10
                    }
                    Text {
                        text: "槽位: " + (App.deviceInfo.slot || "—")
                        color: Theme.textSub
                        font.pixelSize: 10
                    }
                    Text {
                        text: "端口: " + App.flashPorts.length
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                    Text {
                        text: "引导包: " + App.packets.packets.length
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    Text {
                        text: App.activeTasks.length > 0 ? "任务 " + App.activeTasks.length : "就绪"
                        color: App.activeTasks.length > 0 ? Theme.accent : Theme.textMuted
                        font.pixelSize: 10
                    }
                    Text {
                        text: "XMF v" + App.version
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }
            }
        }

        // resize handles above every bar, including the caption
        MouseArea {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 6; z: 5000; enabled: !win.maximized
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.TopEdge)
        }
        MouseArea {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 6; z: 5000; enabled: !win.maximized
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.BottomEdge)
        }
        MouseArea {
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 6; z: 5000; enabled: !win.maximized
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 6; z: 5000; enabled: !win.maximized
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.RightEdge)
        }
        MouseArea {
            anchors.left: parent.left; anchors.top: parent.top
            width: 10; height: 10; z: 5001; enabled: !win.maximized
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right; anchors.top: parent.top
            width: 10; height: 10; z: 5001; enabled: !win.maximized
            cursorShape: Qt.SizeBDiagCursor
            onPressed: win.startSystemResize(Qt.TopEdge | Qt.RightEdge)
        }
        MouseArea {
            anchors.left: parent.left; anchors.bottom: parent.bottom
            width: 10; height: 10; z: 5001; enabled: !win.maximized
            cursorShape: Qt.SizeBDiagCursor
            onPressed: win.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            width: 10; height: 10; z: 5001; enabled: !win.maximized
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
        }
    }

    ToastHost { id: toast; anchors.fill: parent }

    Item {
        id: exitConfirm
        anchors.fill: parent
        visible: false
        z: 3000

        Rectangle {
            anchors.fill: parent
            color: Theme.darkMode ? "#99000000" : "#66000000"
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 400
            height: 160
            radius: Theme.r6
            color: Theme.panel
            border.width: 1
            border.color: Theme.borderStrong

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12
                Text {
                    text: "还有 " + App.activeTasks.length + " 个任务正在运行"
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "刷写过程中退出可能导致设备异常，确定要退出吗？"
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                Row {
                    spacing: 8
                    AppButton { text: "继续等待"; kind: 1; small: true; onClicked: exitConfirm.visible = false }
                    AppButton {
                        text: "取消任务并退出"
                        kind: 3
                        small: true
                        onClicked: {
                            App.cancelAllTasks()
                            win.forceClose = true
                            exitConfirm.visible = false
                            win.close()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: disclaimerOverlay
        anchors.fill: parent
        visible: !App.disclaimerAccepted
        color: "#B3000000"
        z: 10000

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: function(mouse) { mouse.accepted = true }
        }

        GlassSurface {
            width: Math.min(640, parent.width - 80)
            height: Math.min(560, parent.height - 80)
            anchors.centerIn: parent
            title: "免责声明 · 首次使用请阅读"

            Flickable {
                anchors.fill: parent
                anchors.margins: 16
                anchors.bottomMargin: 64
                clip: true
                contentHeight: disclaimerText.height
                TextArea {
                    id: disclaimerText
                    width: parent.width
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: Theme.text
                    font.pixelSize: 12
                    background: null
                    textFormat: TextEdit.PlainText
                    text: "XMF Flash Tool 仅供学习研究与个人设备维护使用。\n\n"
                          + "1. 刷机、解锁、擦除分区等操作存在数据丢失、设备损坏、保修失效的风险，操作前请自行备份重要数据。\n\n"
                          + "2. 请确认固件与设备型号完全匹配。因使用错误固件、错误参数、供电中断或操作失误导致的任何后果，由使用者自行承担。\n\n"
                          + "3. 本工具不绕过厂商签名、账号验证或防回滚机制；解锁、账号相关操作只提供官方可行路径。\n\n"
                          + "4. 使用本工具即表示你已理解并接受上述风险。作者不对因使用本工具造成的直接或间接损失承担责任。\n\n"
                          + "5. 请遵守当地法律法规以及设备厂商的用户协议。"
                }
            }

            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                height: 32

                CheckBox {
                    id: agreeCheck
                    text: "我已阅读并同意以上免责声明"
                    font.pixelSize: 11
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }
                Row {
                    spacing: 8
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    AppButton { text: "退出"; kind: 2; small: true; onClicked: Qt.quit() }
                    AppButton {
                        text: "同意并继续"
                        kind: 0
                        small: true
                        enabled: agreeCheck.checked
                        onClicked: App.acceptDisclaimer()
                    }
                }
            }
        }
    }

    Connections {
        target: App
        function onNotifyRequested(type, message) { toast.show(type, message) }
    }

    Binding { target: Theme; property: "animations"; value: App.settings.animations }
    Binding { target: Theme; property: "darkMode"; value: App.darkTheme }

    NumberAnimation {
        id: introFade
        target: win
        property: "opacity"
        from: 0
        to: 1
        duration: 240
        easing.type: Easing.OutCubic
    }

    Timer {
        interval: 6000
        running: true
        repeat: true
        onTriggered: App.refreshDevices()
    }

    // keyboard navigation for the icon rail
    Shortcut { sequence: "Ctrl+1"; onActivated: win.popToConsole() }
    Shortcut { sequence: "Ctrl+2"; onActivated: win.openPageByKey("tools") }
    Shortcut { sequence: "Ctrl+3"; onActivated: win.openPageByKey("modes") }
    Shortcut { sequence: "Ctrl+4"; onActivated: win.openPageByKey("packets") }
    Shortcut { sequence: "Ctrl+5"; onActivated: win.openPageByKey("firmware") }
    Shortcut { sequence: "Ctrl+6"; onActivated: win.openPageByKey("batch") }
    Shortcut { sequence: "Ctrl+7"; onActivated: win.openPageByKey("logs") }
    Shortcut { sequence: "Ctrl+8"; onActivated: win.openPageByKey("official") }
    Shortcut { sequence: "Ctrl+9"; onActivated: win.openPageByKey("settings") }

    // --------------------------------------------------------- components
    Component { id: pageHostComp; PageHost {} }
    Component { id: toolsComp; ToolsPage { categoryFilter: "" } }
    Component { id: modesComp; ModesPage { onOpenMode: function(modeId) { win.openModePage(modeId) } } }
    Component { id: packetsComp; PacketsPage {} }
    Component { id: firmwareComp; FirmwarePage {} }
    Component { id: batchComp; BatchPage {} }
    Component { id: logsComp; LogsPage {} }
    Component { id: officialComp; OfficialToolsPage { onOpenEmbedded: function(toolId) { win.openEmbeddedTool(toolId) } } }
    Component { id: settingsComp; SettingsPage {} }
    Component { id: consolePageComp; ConsolePage {} }
    Component { id: miflashComp; MiflashModePage { onBackRequested: win.popToConsole() } }
    Component { id: spflashComp; SpFlashModePage { onBackRequested: win.popToConsole() } }
    Component { id: spdflashComp; SpdFlashModePage { onBackRequested: win.popToConsole() } }
    Component { id: rkdevtoolComp; RkDevToolModePage { onBackRequested: win.popToConsole() } }
    Component { id: phoenixsuitComp; PhoenixSuitModePage { onBackRequested: win.popToConsole() } }
    Component { id: fastbootModeComp; FastbootModePage { onBackRequested: win.popToConsole() } }
    Component { id: recoveryModeComp; RecoveryModePage { onBackRequested: win.popToConsole() } }
    Component { id: qcomEdlComp; QualcommEdlModePage { onBackRequested: win.popToConsole() } }
    Component { id: embeddedComp; EmbeddedToolPage { onBackRequested: win.popToConsole() } }
}
