import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import XMF

// 内置模式 · MiFlash 工作台（小米 / 红米 tgz 线刷包）
// 镜像列表来自 App.workspace.loadMiFlashPackage() 的真实解析结果，未加载时为空状态。
Item {
    id: page

    signal backRequested()

    readonly property color modeColor: Theme.accent

    property int modeIndex: 0
    property bool verifyImage: true
    property bool autoReboot: true
    property string tableSubtitle: ""

    readonly property var flashModes: [
        { name: "全部删除", desc: "清除全部数据并刷写所有分区", tag: "CLEAN ALL" },
        { name: "保留用户数据", desc: "保留 data 与内置存储，其余全部刷写", tag: "SAVE USER DATA" },
        { name: "全部删除并锁 BL", desc: "清除数据并重新锁定 Bootloader", tag: "CLEAN ALL + LOCK" }
    ]

    function applyResult(result) {
        table.setPartitions(result.partitions || [])
        page.tableSubtitle = result.subtitle || ""
        frame.fwLoaded = true
        frame.statusText = "已加载 " + table.count + " 个镜像 · 等待设备"
        frame.statusKind = "ready"
        frame.progress = 0
    }

    // modeIndex 0 = 全部删除 / 1 = 保留用户数据 / 2 = 全部删除并锁 BL
    readonly property var modeIds: ["clean", "keep", "clean-lock"]

    function startFlash(onlyPartitions) {
        if (frame.fwPath.length === 0) {
            App.notify("warning", "请先选择小米 / 红米 tgz 线刷包（设备分区读取不能作为刷写来源）")
            logBox.append("warning", "未加载线刷包，无法刷写")
            return
        }
        const opts = ({
            package: frame.fwPath,
            mode: page.modeIds[page.modeIndex] || "clean",
            reboot: page.autoReboot
        })
        if (onlyPartitions && onlyPartitions.length > 0)
            opts.partitions = onlyPartitions
        if (App.fastbootDevices.length > 0)
            opts.serial = App.fastbootDevices[0].serial
        const taskId = App.runTool("miflash.flash", opts)
        if (taskId <= 0) {
            App.notify("error", "刷写任务启动失败，请检查设备是否处于 Fastboot 模式")
            logBox.append("error", "刷写任务启动失败")
            return
        }
        const target = onlyPartitions && onlyPartitions.length > 0
                       ? ("分区 " + onlyPartitions)
                       : (page.flashModes[page.modeIndex].name)
        logBox.append("info", "已提交刷写：" + target + " · 设备 " + (opts.serial || "自动检测"))
        App.notify("info", "刷写已提交，进度见右下角任务面板")
        frame.statusKind = "busy"
        frame.statusText = "刷写中 · " + target
        frame.progress = 0
    }

    function runAction(name) {
        const prefix = "刷写 "
        if (name.indexOf(prefix) === 0) {
            page.startFlash(name.substring(prefix.length).trim())
            return
        }
        if (table.count === 0) {
            App.notify("warning", "请先加载线刷包或读取设备分区")
            return
        }
        logBox.append("info", "触发操作：" + name)
        page.startFlash("")
    }
    function loadPackage(path) {
        if (path.length === 0) {
            packageDialog.open()
            return
        }
        const result = App.workspace.loadMiFlashPackage(path)
        if (!result.ok) {
            App.notify("error", result.error || "线刷包解析失败")
            logBox.append("error", result.error || "线刷包解析失败")
            return
        }
        frame.fwPath = path
        applyResult(result)
        logBox.append("success", "线刷包解析完成：" + page.tableSubtitle)
        App.notify("success", "线刷包已加载，可直接点击「刷机」")
        frame.statusKind = "ready"
        frame.statusText = "线刷包已加载 · 等待刷写"
        if (App.fastbootDevices.length === 0)
            logBox.append("warning", "当前没有 Fastboot 设备，刷写前请先把手机切到 Fastboot 模式")
    }

    function reloadPackage() {
        if (frame.fwPath.length === 0)
            packageDialog.open()
        else
            loadPackage(frame.fwPath)
    }

    function scanDevice() {
        if (App.adbDevices.length === 0) {
            App.notify("warning", "未检测到设备，请检查 USB 与驱动")
            logBox.append("warning", "未检测到设备，请检查 USB 与驱动")
            return
        }
        const result = App.workspace.loadAdbPartitions("")
        if (!result.ok) {
            App.notify("error", result.error || "设备分区读取失败")
            return
        }
        frame.fwPath = ""
        frame.fwLoaded = false
        table.setPartitions(result.partitions || [])
        page.tableSubtitle = result.subtitle || ""
        frame.statusText = "已读取设备分区 · 未加载线刷包"
        frame.statusKind = "ready"
        logBox.append("success", "设备分区：" + page.tableSubtitle)
    }

    ModeFrame {
        id: frame
        anchors.fill: parent

        title: "MiFlash 模式"
        subtitle: "小米 / 红米 tgz 线刷包 · 全部删除 / 保留数据 / 删除并锁 BL"
        icon: "smartphone"
        tag: "FASTBOOT"
        accentColor: page.modeColor

        firmwareLabel: "线刷包"
        firmwarePlaceholder: "选择小米线刷包（*.tgz / *.tar.gz）"
        fwPath: ""
        fwLoaded: false

        progress: 0
        statusText: "就绪 · 未加载线刷包"
        statusKind: "idle"
        deviceText: App.adbDevices.length > 0 ? "已连接设备" : "未检测到设备"
        deviceKind: App.adbDevices.length > 0 ? "ok" : "idle"
        speedText: "—"
        deviceCode: App.adbDevices.length > 0 ? String(App.adbDevices[0].device || App.adbDevices[0].model || App.adbDevices[0].serial || "—") : "—"
        bootloaderText: "未知"
        slotText: "—"

        onBackRequested: page.backRequested()
        onBrowseRequested: packageDialog.open()
        onLoadRequested: page.reloadPackage()
        onReloadRequested: page.scanDevice()

        headerActions: [
            AppButton {
                text: "刷新设备"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: page.scanDevice()
            },
            AppButton {
                text: "刷机"
                icon: "zap"
                kind: 0
                small: true
                onClicked: {
                    if (table.count === 0) {
                        App.notify("warning", "请先加载线刷包或读取设备分区")
                        return
                    }
                    logBox.append("info", "准备刷机：模式「" + page.flashModes[page.modeIndex].name + "」")
                    page.startFlash("")
                }
            }
        ]

        // ------------------------------------------------------------ 内容
        RowLayout {
            anchors.fill: parent
            spacing: 12

            // ---- 镜像列表
            ModeCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "镜像列表"
                subtitle: page.tableSubtitle.length > 0 ? page.tableSubtitle : "尚未加载线刷包"
                icon: "hard-drive"
                accentColor: page.modeColor

                actions: [
                    Rectangle {
                        width: 62
                        height: 18
                        radius: Theme.r10
                        color: Theme.glassStrong
                        border.width: 1
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: "FASTBOOT"
                            color: Theme.textMuted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                    }
                ]

                ModePartitionTable {
                    id: table
                    anchors.fill: parent
                    columns: [
                        { label: "LUN", role: "lun", width: 78 },
                        { label: "名称", role: "name", flex: true },
                        { label: "大小", role: "size", width: 68, align: "right", mono: true },
                        { label: "镜像文件", role: "file", width: 150, mono: true },
                        { label: "操作", role: "action", width: 44, align: "right" }
                    ]
                    emptyIcon: "file-archive"
                    emptyTitle: "尚未加载线刷包"
                    emptyText: "选择小米 / 红米 .tgz 线刷包后，这里会列出包内的镜像文件；也可以刷新已连接设备的分区。"
                    emptyActionText: "选择线刷包"
                    onEmptyAction: packageDialog.open()
                    onRowAction: function (index) {
                        const row = table.rowAt(index)
                        page.runAction("刷写 " + (row ? row.name : ""))
                    }
                }
            }

            // ---- 右侧：刷机模式 + 设备 + 日志
            Item {
                Layout.preferredWidth: 330
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 250
                        title: "刷机模式"
                        icon: "sliders-horizontal"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            Repeater {
                                model: page.flashModes

                                delegate: Rectangle {
                                    id: modeRow
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 46
                                    radius: Theme.r10
                                    color: page.modeIndex === index
                                           ? Theme.accentSoft
                                           : (modeRowMouse.containsMouse ? Theme.glassHover : "transparent")
                                    border.width: page.modeIndex === index ? 1 : 0
                                    border.color: Theme.borderStrong

                                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

                                    Row {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 10

                                        Rectangle {
                                            width: 16
                                            height: 16
                                            radius: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: "transparent"
                                            border.width: 1
                                            border.color: page.modeIndex === index ? Theme.accent : Theme.borderStrong

                                            Rectangle {
                                                anchors.centerIn: parent
                                                visible: page.modeIndex === index
                                                width: 8
                                                height: 8
                                                radius: 4
                                                color: Theme.accent
                                            }
                                        }

                                        Column {
                                            width: parent.width - 36
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 1

                                            Text {
                                                text: modelData.name
                                                color: page.modeIndex === index ? Theme.text : Theme.textSub
                                                font.pixelSize: 11
                                                font.weight: Font.Medium
                                            }
                                            Text {
                                                width: parent.width
                                                text: modelData.desc
                                                color: Theme.textMuted
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: modeRowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            page.modeIndex = index
                                            logBox.append("debug", "切换刷机模式：" + modelData.name)
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.divider
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: "刷写前校验镜像哈希"
                                    color: Theme.textSub
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                                ToggleSwitch {
                                    Layout.alignment: Qt.AlignVCenter
                                    checked: page.verifyImage
                                    accentColor: Theme.textMuted
                                    onToggled: function (value) { page.verifyImage = value }
                                }
                            }
                        }
                    }

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        title: "设备"
                        icon: "usb"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: App.adbDevices.length > 0 ? "ok" : "idle"
                                label: "ADB 设备"
                                detail: App.adbDevices.length > 0 ? ("已连接 " + App.adbDevices.length + " 台") : "未连接"
                            }
                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "fastboot 设备"
                                detail: App.fastbootDevices.length > 0
                                        ? String(App.fastbootDevices[0].serial || "已连接")
                                        : "未连接"
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                Layout.fillWidth: true
                                text: "端口 COM — · 驱动待检测"
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }

                    ModeLog {
                        id: logBox
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        accentColor: page.modeColor
                        seedLines: [
                            { level: "info", message: "MiFlash 工作台已就绪，等待加载线刷包" }
                        ]
                    }
                }
            }
        }
    }

    FileDialog {
        id: packageDialog
        title: "选择小米线刷包"
        nameFilters: [
            "小米线刷包 (*.tgz *.tar.gz)",
            "所有文件 (*.*)"
        ]
        onAccepted: page.loadPackage(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }
}
