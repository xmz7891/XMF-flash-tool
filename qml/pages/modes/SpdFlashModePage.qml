import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import XMF

// 内置模式 · SPD Flash Tool 工作台（紫光展锐 Unisoc PAC 固件）
// 镜像表来自 App.workspace.loadPac() 的真实解析结果，未加载时为空状态。
Item {
    id: page

    signal backRequested()

    readonly property color modeColor: Theme.accent

    property string tableSubtitle: ""

    function loadPac(path) {
        if (path.length === 0) {
            pacDialog.open()
            return
        }
        const result = App.workspace.loadPac(path)
        if (!result.ok) {
            App.notify("error", result.error || "PAC 解析失败")
            logBox.append("error", result.error || "PAC 解析失败")
            return
        }
        frame.fwPath = path
        frame.fwLoaded = true
        frame.statusText = "PAC 已加载 · 等待 FDL 握手"
        frame.statusKind = "ready"
        frame.progress = 0
        table.setPartitions(result.partitions || [])
        page.tableSubtitle = result.subtitle || ""
        logBox.append("success", "PAC 解析完成：" + page.tableSubtitle)
        App.notify("success", "PAC 已加载，可点击「全部下载」开始刷写（需 FDL 模式）")
    }

    function reloadPac() {
        if (frame.fwPath.length === 0)
            pacDialog.open()
        else
            loadPac(frame.fwPath)
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
        frame.statusText = "已读取设备分区 · 未加载 PAC"
        frame.statusKind = "ready"
        logBox.append("success", "设备分区：" + page.tableSubtitle)
    }

    function runAction(name) {
        if (table.count === 0) {
            App.notify("warning", "请先加载 PAC 固件或读取设备分区")
            return
        }
        if (frame.fwPath.length === 0) {
            App.notify("warning", "展锐刷写需要先加载 PAC 固件（FDL / U2S 模式）")
            logBox.append("warning", "未加载 PAC，无法执行：" + name)
            return
        }
        const opts = ({ pac: frame.fwPath })
        const taskId = App.runTool("sprd.flow.flash", opts)
        if (taskId <= 0) {
            App.notify("error", "刷写任务启动失败")
            logBox.append("error", "刷写任务启动失败")
            return
        }
        logBox.append("info", "已提交展锐刷写任务：" + name)
        App.notify("info", "展锐刷写已提交，进度见任务面板；请保持手机在 FDL 模式")
        frame.statusKind = "busy"
        frame.statusText = "刷写中 · " + name
        frame.progress = 0
    }

    ModeFrame {
        id: frame
        anchors.fill: parent

        title: "SPD Flash Tool"
        subtitle: "紫光展锐 PAC 固件 · FDL1 / FDL2 下载"
        icon: "layers"
        tag: "FDL"
        accentColor: page.modeColor

        firmwareLabel: "PAC"
        firmwarePlaceholder: "选择展锐 PAC 固件（*.pac）"
        fwPath: ""
        fwLoaded: false

        progress: 0
        statusText: "就绪 · 未加载 PAC"
        statusKind: "idle"
        deviceText: App.adbDevices.length > 0 ? "已连接设备" : "未检测到设备"
        deviceKind: App.adbDevices.length > 0 ? "ok" : "idle"
        speedText: "—"
        deviceCode: App.adbDevices.length > 0 ? String(App.adbDevices[0].device || App.adbDevices[0].model || App.adbDevices[0].serial || "—") : "—"
        bootloaderText: "未知"
        slotText: "—"

        onBackRequested: page.backRequested()
        onBrowseRequested: pacDialog.open()
        onLoadRequested: page.reloadPac()
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
                text: "全部下载"
                icon: "download"
                kind: 0
                small: true
                onClicked: page.runAction("全部下载")
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: 12

            // ---- PAC 镜像表
            ModeCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "PAC 镜像表"
                subtitle: page.tableSubtitle.length > 0 ? page.tableSubtitle : "尚未加载 PAC"
                icon: "layers"
                accentColor: page.modeColor

                actions: [
                    ModeCheckBox {
                        width: 16
                        height: 16
                        visible: table.count > 0
                        checked: table.allSelected
                        onToggled: function (value) { table.setAll(value) }
                    },
                    Text {
                        visible: table.count > 0
                        text: "全选"
                        color: table.allSelected ? Theme.text : Theme.textMuted
                        font.pixelSize: 10
                    },
                    Text {
                        visible: table.count > 0
                        text: table.selectedCount + " / " + table.count
                        color: Theme.accent
                        font.pixelSize: 10
                        font.family: "Consolas"
                    }
                ]

                ModePartitionTable {
                    id: table
                    anchors.fill: parent
                    checkable: true
                    columns: [
                        { label: "LUN", role: "lun", width: 78 },
                        { label: "名称", role: "name", flex: true },
                        { label: "大小", role: "size", width: 68, align: "right", mono: true },
                        { label: "起始偏移", role: "address", width: 84, mono: true },
                        { label: "操作", role: "action", width: 44, align: "right" }
                    ]
                    emptyIcon: "file-archive"
                    emptyTitle: "尚未加载 PAC 固件"
                    emptyText: "选择展锐 .pac 固件后，这里会列出 PAC 内的镜像、偏移与类型；也可以刷新已连接设备的分区。"
                    emptyActionText: "选择 PAC 固件"
                    onEmptyAction: pacDialog.open()
                    onRowAction: function (index) {
                        const row = table.rowAt(index)
                        page.runAction("刷写 " + (row ? row.name : ""))
                    }
                }
            }

            // ---- 右侧：FDL 状态 + 操作 + 结果 + 日志
            Item {
                Layout.preferredWidth: 330
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 126
                        title: "FDL 状态"
                        icon: "activity"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "FDL1"
                                detail: "未加载"
                            }
                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "FDL2"
                                detail: "未加载"
                            }
                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: App.adbDevices.length > 0 ? "ok" : "idle"
                                label: "ADB 设备"
                                detail: App.adbDevices.length > 0 ? ("已连接 " + App.adbDevices.length) : "未连接"
                            }
                        }
                    }

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 152
                        title: "操作"
                        icon: "sliders-horizontal"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppButton {
                                    Layout.fillWidth: true
                                    text: "全部下载"
                                    icon: "download"
                                    kind: 0
                                    small: true
                                    onClicked: page.runAction("全部下载")
                                }
                                AppButton {
                                    Layout.fillWidth: true
                                    text: "单分区"
                                    icon: "list-checks"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("单分区下载")
                                }
                            }

                                                        Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.divider
                            }
RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppButton {
                                    Layout.fillWidth: true
                                    text: "备份分区"
                                    icon: "hard-drive-upload"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("备份分区")
                                }
                                AppButton {
                                    Layout.fillWidth: true
                                    text: "格式化"
                                    icon: "trash-2"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("格式化")
                                }
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                Layout.fillWidth: true
                                text: table.count > 0 ? ("已选 " + table.selectedCount + " 个分区") : "尚未加载固件"
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 86
                        title: "最近结果"
                        icon: "badge-check"
                        accentColor: page.modeColor

                        RowLayout {
                            anchors.fill: parent
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 64
                                Layout.preferredHeight: 30
                                Layout.alignment: Qt.AlignVCenter
                                radius: Theme.r10
                                color: Theme.glassStrong
                                border.width: 1
                                border.color: Theme.border

                                Text {
                                    anchors.centerIn: parent
                                    text: "—"
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                            }

                            Column {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2

                                Text {
                                    width: parent.width
                                    text: "尚无刷机记录"
                                    color: Theme.text
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: "执行下载后在这里显示 PASS / FAIL"
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    ModeLog {
                        id: logBox
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        accentColor: page.modeColor
                        seedLines: [
                            { level: "info", message: "SPD Flash Tool 工作台已就绪，等待加载 PAC 固件" }
                        ]
                    }
                }
            }
        }
    }

    FileDialog {
        id: pacDialog
        title: "选择展锐 PAC 固件"
        nameFilters: [
            "展锐固件 (*.pac)",
            "所有文件 (*.*)"
        ]
        onAccepted: page.loadPac(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }
}
