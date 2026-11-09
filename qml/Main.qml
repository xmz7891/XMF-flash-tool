import QtQuick
import QtQuick.Controls.Basic
import XMF

Window {
    id: win
    width: 1400
    height: 900
    minimumWidth: 1120
    minimumHeight: 720
    visible: true
    color: Theme.windowBg
    title: "XMF Flash Tool"
    flags: Qt.Window | Qt.FramelessWindowHint

    readonly property bool maximized: visibility === Window.Maximized
    property int navIndex: 0
    property bool forceClose: false

    onClosing: function(close) {
        if (!forceClose && App.activeTasks.length > 0) {
            close.accepted = false
            exitConfirm.visible = true
        }
    }

    readonly property var navModel: [
        { key: "home",     label: "概览",        icon: "home" },
        { key: "flash",    label: "刷机中心",    icon: "zap" },
        { key: "image",    label: "工具库",      icon: "disc-3" },
        { key: "modes",    label: "内置模式",    icon: "layout-dashboard" },
        { key: "packets",  label: "深度刷机引导",  icon: "package" },
        { key: "firmware", label: "固件资源",    icon: "download-cloud" },
        { key: "batch",    label: "批量任务",    icon: "list-checks" },
        { key: "logs",     label: "日志",        icon: "file-text" },
        { key: "official", label: "官方工具",    icon: "box" },
        { key: "settings", label: "设置",        icon: "settings" },
        { key: "console",  label: "控制台",      icon: "terminal" }
    ]

    function pageComponent(index) {
        switch (index) {
        case 0: return homeComp
        case 1: return flashComp
        case 2: return imageComp
        case 3: return modesComp
        case 4: return packetsComp
        case 5: return firmwareComp
        case 6: return batchComp
        case 7: return logsComp
        case 8: return officialComp
        case 9: return settingsComp
        case 10: return consoleComp
        }
        return homeComp
    }

    function navigate(index) {
        if (index === navIndex)
            return
        navIndex = index
        stack.replace(pageComponent(index))
    }

    function openEmbeddedTool(toolId) {
        stack.push(embeddedComp, { "toolId": toolId })
    }

    // Test hook: navigate to the flash center and read the connected device's
    // partitions with the same code path the button uses.
    function autoReadDevicePartitions() {
        navigate(1)
        Qt.callLater(function() {
            const page = stack.currentItem
            if (page && page.loadDevicePartitions)
                page.loadDevicePartitions()
        })
    }

    // Flash center: Fastboot / Recovery / 9008(EDL) are separate workbenches.
    function openFlashMode(modeId) {
        const id = String(modeId)
        if (id.indexOf("fastboot:") === 0) {
            stack.push(fastbootModeComp, { "platform": id.substring("fastboot:".length) })
            return
        }
        if (id.indexOf("recovery:") === 0) {
            stack.push(recoveryModeComp, { "platform": id.substring("recovery:".length) })
            return
        }
        openModePage(id)
    }

    function openModePage(modeId) {
        switch (modeId) {
        case "miflash": stack.push(miflashComp); break
        case "spflash": stack.push(spflashComp); break
        case "spdflash": stack.push(spdflashComp); break
        case "rkdevtool": stack.push(rkdevtoolComp); break
        case "phoenixsuit": stack.push(phoenixsuitComp); break
        case "fastboot": stack.push(fastbootModeComp, { "platform": "qcom" }); break
        case "recovery": stack.push(recoveryModeComp, { "platform": "qcom" }); break
        case "qcom-edl": stack.push(qcomEdlComp); break
        }
    }

    function toggleMaximize() {
        if (maximized)
            win.showNormal()
        else
            win.showMaximized()
    }

    // ------------------------------------------------------------------ shell
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: win.maximized ? 0 : 6
        color: Theme.windowBg
        border.width: 1
        border.color: Theme.windowBorder
        clip: true

        // -------------------------------------------------------- title bar
        Item {
            id: titleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 32

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onPressed: win.startSystemMove()
                onDoubleClicked: win.toggleMaximize()
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Rectangle {
                    width: 18; height: 18
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
                    color: Theme.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "v" + App.version
                    color: Theme.textMuted
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
                anchors.rightMargin: 0
                anchors.top: parent.top
                spacing: 0

                Item {
                    width: 34; height: 32
                    Rectangle {
                        anchors.fill: parent
                        color: themeMouse.containsMouse ? Theme.panelHover : "transparent"
                    }
                    IconImage {
                        anchors.centerIn: parent
                        name: Theme.darkMode ? "sun" : "moon"
                        size: 13
                        tint: Theme.textSub
                    }
                    MouseArea {
                        id: themeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.settings.themeMode = Theme.darkMode ? "light" : "dark"
                    }
                }

                WindowButton { kind: 0; implicitHeight: 32; onClicked: win.showMinimized() }
                WindowButton { kind: 1; implicitHeight: 32; onClicked: win.toggleMaximize() }
                WindowButton { kind: 2; implicitHeight: 32; onClicked: win.close() }
            }
        }

        // -------------------------------------------------------- menu bar
        Item {
            id: menuBar
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 24

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.divider
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Repeater {
                    model: [
                        { label: "文件(F)", menu: fileMenu },
                        { label: "设备(D)", menu: deviceMenu },
                        { label: "工具(T)", menu: toolsMenu },
                        { label: "视图(V)", menu: viewMenu },
                        { label: "帮助(H)", menu: helpMenu }
                    ]
                    delegate: Item {
                        width: menuText.implicitWidth + 18
                        height: 24
                        Rectangle {
                            anchors.fill: parent
                            color: mbMouse.containsMouse || modelData.menu.visible ? Theme.panelHover : "transparent"
                        }
                        Text {
                            id: menuText
                            anchors.centerIn: parent
                            text: modelData.label
                            color: Theme.text
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: mbMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.menu.visible)
                                    modelData.menu.close()
                                else
                                    modelData.menu.popup()
                            }
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------- toolbar
        Item {
            id: toolBar
            anchors.top: menuBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 36
            Rectangle {
                anchors.fill: parent
                color: Theme.toolbar
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                AppButton { text: "刷新设备"; icon: "refresh-cw"; kind: 1; small: true; onClicked: { App.scanPorts(); App.refreshDevices() } }
                AppButton { text: "读取分区"; icon: "hard-drive"; kind: 1; small: true; onClicked: win.navigate(1) }
                AppButton { text: "分区备份"; icon: "hard-drive-download"; kind: 1; small: true; onClicked: win.navigate(2) }
                AppButton { text: "刷写固件"; icon: "file-up"; kind: 0; small: true; onClicked: win.navigate(3) }
                Rectangle { width: 1; height: 18; color: Theme.border }
                AppButton { text: "引导包"; icon: "package"; kind: 2; small: true; onClicked: win.navigate(4) }
                AppButton { text: "官方工具"; icon: "box"; kind: 2; small: true; onClicked: win.navigate(8) }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text {
                    text: "端口 " + App.flashPorts.length + "   ADB " + App.adbDevices.length
                          + "   Fastboot " + App.fastbootDevices.length
                    color: Theme.textMuted
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // -------------------------------------------------------- body
        Row {
            anchors.top: toolBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: statusBar.top

            // sidebar
            Rectangle {
                id: sidebar
                width: 176
                height: parent.height
                color: Theme.sidebar
                border.width: 0

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.divider
                }

                Column {
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 0

                    Repeater {
                        model: win.navModel
                        delegate: NavItem {
                            text: modelData.label
                            icon: modelData.icon
                            active: win.navIndex === index
                            onClicked: win.navigate(index)
                        }
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    spacing: 3
                    Text {
                        text: App.deviceInfo.connected
                              ? ("● " + (App.deviceInfo.model || App.deviceInfo.serial || "设备已连接"))
                              : "○ 未连接设备"
                        color: App.deviceInfo.connected ? Theme.success : Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: App.flashPorts.length + " 端口 · " + App.driverSummaryText()
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
            }

            // content
            Item {
                id: contentArea
                width: parent.width - sidebar.width
                height: parent.height
                Rectangle {
                    anchors.fill: parent
                    color: Theme.windowBg
                }

                Item {
                    anchors.fill: parent
                    anchors.margins: 8

                    StackView {
                        id: stack
                        anchors.fill: parent
                        initialItem: homeComp

                        pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 } }
                        pushExit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 60 } }
                        popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 } }
                        popExit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 60 } }
                        replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 } }
                        replaceExit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 60 } }
                    }
                }
            }
        }

        // -------------------------------------------------------- status bar
        Item {
            id: statusBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 22

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.border
            }
            Rectangle {
                anchors.fill: parent
                color: Theme.toolbar
                z: -1
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                Text {
                    text: App.deviceInfo.connected
                          ? ("设备: " + (App.deviceInfo.model || App.deviceInfo.serial || "-"))
                            + (App.deviceInfo.mode === "fastboot" ? "  [Fastboot]" : "")
                          : "设备: 未连接"
                    color: Theme.textSub
                    font.pixelSize: 10
                }
                Text {
                    text: "BL: " + (App.deviceInfo.bootloader || "—")
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Text {
                    text: "槽位: " + (App.deviceInfo.slot || "—")
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Text {
                    text: "端口: " + App.flashPorts.length
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Text {
                    text: "驱动: " + App.driverSummaryText()
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Text {
                    visible: (App.deviceInfo.battery || "").length > 0
                    text: "电量: " + App.deviceInfo.battery + " mV"
                    color: App.deviceInfo.batteryOk === false ? Theme.danger : Theme.textMuted
                    font.pixelSize: 10
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text {
                    text: "XMF v" + App.version
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }
        }

        TaskDock {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: statusBar.top
            anchors.bottomMargin: 10
        }

        ToastHost { id: toast }

        // 有任务运行时退出确认，避免刷写中途关掉程序
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
                width: 380
                height: 150
                radius: Theme.r16
                color: Theme.panel
                border.width: 1
                border.color: Theme.borderStrong

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    text: "还有 " + App.activeTasks.length + " 个任务正在运行"
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 44
                    anchors.margins: 16
                    text: "此时退出可能中断刷写并让设备停留在半刷状态。"
                    color: Theme.textSub
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 14
                    spacing: 8
                    AppButton {
                        text: "继续等待"
                        kind: 1
                        onClicked: exitConfirm.visible = false
                    }
                    AppButton {
                        text: "取消任务并退出"
                        kind: 3
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

        // resize handles
        MouseArea { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; cursorShape: Qt.SizeHorCursor; onPressed: win.startSystemResize(Qt.LeftEdge) }
        MouseArea { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; cursorShape: Qt.SizeHorCursor; onPressed: win.startSystemResize(Qt.RightEdge) }
        MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 32; height: 4; cursorShape: Qt.SizeVerCursor; onPressed: win.startSystemResize(Qt.TopEdge) }
        MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 4; cursorShape: Qt.SizeVerCursor; onPressed: win.startSystemResize(Qt.BottomEdge) }
    }

    // ------------------------------------------------------------------ menus
    Menu { id: fileMenu; width: 190
        MenuItem { text: "打开日志目录"; onTriggered: App.openPath(App.dataPath) }
        MenuItem { text: "打开数据目录"; onTriggered: App.openPath(App.dataPath) }
        MenuSeparator { }
        MenuItem { text: "退出"; onTriggered: win.close() }
    }
    Menu { id: deviceMenu; width: 190
        MenuItem { text: "刷新端口"; onTriggered: App.scanPorts() }
        MenuItem { text: "刷新 ADB / Fastboot"; onTriggered: App.refreshDevices() }
        MenuItem { text: "读取设备分区"; onTriggered: win.navigate(1) }
        MenuSeparator { }
        MenuItem { text: "安装驱动"; onTriggered: App.installDrivers() }
    }
    Menu { id: toolsMenu; width: 190
        MenuItem { text: "工具库"; onTriggered: win.navigate(2) }
        MenuItem { text: "内置模式"; onTriggered: win.navigate(3) }
        MenuItem { text: "官方工具"; onTriggered: win.navigate(8) }
        MenuItem { text: "命令控制台"; onTriggered: win.navigate(10) }
        MenuItem { text: "批量任务"; onTriggered: win.navigate(6) }
    }
    Menu { id: viewMenu; width: 190
        MenuItem { text: "浅色主题"; onTriggered: App.settings.themeMode = "light" }
        MenuItem { text: "深色主题"; onTriggered: App.settings.themeMode = "dark" }
        MenuItem { text: "跟随系统"; onTriggered: App.settings.themeMode = "auto" }
        MenuSeparator { }
        MenuItem { text: "日志"; onTriggered: win.navigate(7) }
        MenuItem { text: "设置"; onTriggered: win.navigate(9) }
    }
    Menu { id: helpMenu; width: 190
        MenuItem {
            text: "使用指南"
            onTriggered: {
                if (App.openDocs("USER_GUIDE.md").length === 0)
                    App.notify("error", "未找到 docs/USER_GUIDE.md")
            }
        }
        MenuItem {
            text: "更新记录"
            onTriggered: {
                if (App.openDocs("CHANGELOG.md").length === 0)
                    App.notify("error", "未找到 docs/CHANGELOG.md")
            }
        }
        MenuItem {
            text: "功能覆盖矩阵"
            onTriggered: {
                if (App.openDocs("FEATURE_COVERAGE.md").length === 0)
                    App.notify("error", "未找到 docs/FEATURE_COVERAGE.md")
            }
        }
        MenuItem { text: "数据与日志目录"; onTriggered: App.openPath(App.dataPath) }
        MenuItem {
            text: "运行环境自检"
            onTriggered: {
                if (App.runTool("system.selfTest", ({})) > 0)
                    App.notify("info", "环境自检已开始，结果见日志与任务面板")
                else
                    App.notify("error", "环境自检启动失败")
            }
        }
        MenuItem {
            text: "导出诊断信息"
            onTriggered: {
                const path = App.writeDiagnostics()
                if (path.length > 0) {
                    App.notify("success", "诊断报告已导出")
                    App.revealPath(path)
                } else {
                    App.notify("error", "诊断报告导出失败")
                }
            }
        }
        MenuItem { text: "关于 XMF"; onTriggered: App.notify("info", "XMF Flash Tool v" + App.version) }
    }

    // ---------------------------------------------------------- disclaimer
    Rectangle {
        id: disclaimerOverlay
        anchors.fill: parent
        visible: !App.disclaimerAccepted
        color: "#B3000000"
        z: 10000

        // Block every interaction with the main UI until the user agrees.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: function(mouse) { mouse.accepted = true }
            onWheel: function(wheel) { wheel.accepted = true }
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
                boundsBehavior: Flickable.StopAtBounds

                TextArea {
                    id: disclaimerText
                    width: parent.width
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: Theme.text
                    font.pixelSize: 11
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

                Item {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
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
                        AppButton {
                            text: "退出"
                            kind: 2
                            small: true
                            onClicked: Qt.quit()
                        }
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
    }

    // ---------------------------------------------------------------- signals
    Connections {
        target: App
        function onNotifyRequested(type, message) { toast.show(type, message) }
    }

    Binding { target: Theme; property: "animations"; value: App.settings.animations }
    Binding { target: Theme; property: "darkMode"; value: App.darkTheme }

    Timer {
        interval: 6000
        running: true
        repeat: true
        onTriggered: App.refreshDevices()
    }

    // -------------------------------------------------------------- components
    Component { id: homeComp; DashboardPage { onNavigateTo: function(i) { win.navigate(i) } } }
    Component { id: flashComp; FlashCenterPage { onOpenFlashMode: function(modeId) { win.openFlashMode(modeId) } } }
    Component { id: imageComp; ToolsPage { categoryFilter: "" } }
    Component { id: modesComp; ModesPage { onOpenMode: function(modeId) { win.openModePage(modeId) } } }
    Component { id: fastbootModeComp; FastbootModePage { onBackRequested: stack.pop() } }
    Component { id: recoveryModeComp; RecoveryModePage { onBackRequested: stack.pop() } }
    Component { id: qcomEdlComp; QualcommEdlModePage { onBackRequested: stack.pop() } }
    Component { id: packetsComp; PacketsPage {} }
    Component { id: firmwareComp; FirmwarePage {} }
    Component { id: batchComp; BatchPage {} }
    Component { id: logsComp; LogsPage {} }
    Component { id: officialComp; OfficialToolsPage { onOpenEmbedded: function(toolId) { win.openEmbeddedTool(toolId) } } }
    Component { id: settingsComp; SettingsPage {} }
    Component { id: consoleComp; ConsolePage {} }

    Component { id: miflashComp; MiflashModePage { onBackRequested: stack.pop() } }
    Component { id: spflashComp; SpFlashModePage { onBackRequested: stack.pop() } }
    Component { id: spdflashComp; SpdFlashModePage { onBackRequested: stack.pop() } }
    Component { id: rkdevtoolComp; RkDevToolModePage { onBackRequested: stack.pop() } }
    Component { id: phoenixsuitComp; PhoenixSuitModePage { onBackRequested: stack.pop() } }
    Component { id: embeddedComp; EmbeddedToolPage { onBackRequested: stack.pop() } }
}
