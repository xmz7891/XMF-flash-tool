import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import XMF

// 内置模式 · SP Flash Tool 工作台（联发科 MTK scatter 固件）
// 分区表来自 App.workspace.loadScatter() 的真实解析结果，未加载时为空状态。
Item {
    id: page

    signal backRequested()

    readonly property color modeColor: Theme.accent

    property bool verifyImage: true
    property string tableSubtitle: ""

    function loadScatter(path) {
        if (path.length === 0) {
            scatterDialog.open()
            return
        }
        const result = App.workspace.loadScatter(path)
        if (!result.ok) {
            App.notify("error", result.error || "scatter 解析失败")
            logBox.append("error", result.error || "scatter 解析失败")
            return
        }
        frame.fwPath = path
        frame.fwLoaded = true
        frame.statusText = "scatter 已加载 · 等待 BROM 设备"
        frame.statusKind = "ready"
        frame.progress = 0
        table.setPartitions(result.partitions || [])
        page.tableSubtitle = result.subtitle || ""
        logBox.append("success", "scatter 解析完成：" + page.tableSubtitle)
        App.notify("success", "scatter 已加载，可点击「全部下载」开始刷写（需 BROM / 预载模式）")
    }

    function reloadScatter() {
        if (frame.fwPath.length === 0)
            scatterDialog.open()
        else
            loadScatter(frame.fwPath)
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
        frame.statusText = "已读取设备分区 · 未加载 scatter"
        frame.statusKind = "ready"
        logBox.append("success", "设备分区：" + page.tableSubtitle)
    }

    function runAction(name) {
        if (table.count === 0) {
            App.notify("warning", "请先加载 scatter 分区表或读取设备分区")
            return
        }
        if (frame.fwPath.length === 0) {
            App.notify("warning", "MTK 刷写需要先加载 scatter 文件（BROM / DA 模式）")
            logBox.append("warning", "未加载 scatter，无法执行：" + name)
            return
        }
        // 全部下载 / 仅刷选定 / 格式化+下载 都走同一条 MTK 流水线；分区子集由
        // flows 的 partition 参数控制，引导包/DA 由工具自动解析。
        const opts = ({ scatter: frame.fwPath })
        const taskId = App.runTool("mtk.flow.flash", opts)
        if (taskId <= 0) {
            App.notify("error", "刷写任务启动失败")
            logBox.append("error", "刷写任务启动失败")
            return
        }
        logBox.append("info", "已提交 MTK 刷写任务：" + name)
        App.notify("info", "MTK 刷写已提交，进度见任务面板；请保持手机在 BROM / 预载模式")
        frame.statusKind = "busy"
        frame.statusText = "刷写中 · " + name
        frame.progress = 0
    }

    ModeFrame {
        id: frame
        anchors.fill: parent

        title: "SP Flash Tool"
        subtitle: "联发科 scatter 分区表 · BROM / DA 下载"
        icon: "microchip"
        tag: "BROM"
        accentColor: page.modeColor

        firmwareLabel: "Scatter"
        firmwarePlaceholder: "选择 MTK scatter 分区表（scatter.txt）"
        fwPath: ""
        fwLoaded: false

        progress: 0
        statusText: "就绪 · 未加载 scatter"
        statusKind: "idle"
        deviceText: App.adbDevices.length > 0 ? "已连接设备" : "未检测到设备"
        deviceKind: App.adbDevices.length > 0 ? "ok" : "idle"
        speedText: "—"
        deviceCode: App.adbDevices.length > 0 ? String(App.adbDevices[0].device || App.adbDevices[0].model || App.adbDevices[0].serial || "—") : "—"
        bootloaderText: "未知"
        slotText: "—"

        onBackRequested: page.backRequested()
        onBrowseRequested: scatterDialog.open()
        onLoadRequested: page.reloadScatter()
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
                text: "下载"
                icon: "download"
                kind: 0
                small: true
                onClicked: page.runAction("全部下载")
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: 12

            // ---- scatter 分区表
            ModeCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "Scatter 分区表"
                subtitle: page.tableSubtitle.length > 0 ? page.tableSubtitle : "尚未加载 scatter.txt"
                icon: "list-checks"
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
                        { label: "镜像文件", role: "file", width: 150, mono: true },
                        { label: "操作", role: "action", width: 44, align: "right" }
                    ]
                    emptyIcon: "file-archive"
                    emptyTitle: "尚未加载 scatter 分区表"
                    emptyText: "点击下方按钮选择 MTK scatter.txt，解析后在这里显示分区、大小与镜像文件；也可以刷新已连接设备的分区。"
                    emptyActionText: "选择 scatter.txt"
                    onEmptyAction: scatterDialog.open()
                    onRowAction: function (index) {
                        const row = table.rowAt(index)
                        page.runAction("刷写 " + (row ? row.name : ""))
                    }
                }
            }

            // ---- 右侧：下载操作 + BROM 状态 + 日志
            Item {
                Layout.preferredWidth: 330
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 240
                        title: "下载操作"
                        icon: "download"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            AppButton {
                                Layout.fillWidth: true
                                text: "下载"
                                icon: "download"
                                kind: 0
                                small: true
                                onClicked: page.runAction("全部下载")
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.divider
                            }

                            AppButton {
                                Layout.fillWidth: true
                                text: "仅刷选定分区"
                                icon: "list-checks"
                                kind: 1
                                small: true
                                onClicked: page.runAction("仅刷选定分区（" + table.selectedCount + " 个）")
                            }
                            AppButton {
                                Layout.fillWidth: true
                                text: "格式化 + 下载"
                                icon: "hard-drive-download"
                                kind: 1
                                small: true
                                onClicked: page.runAction("格式化 + 下载")
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppButton {
                                    Layout.fillWidth: true
                                    text: "读回"
                                    icon: "upload"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("分区读回")
                                }
                                AppButton {
                                    Layout.fillWidth: true
                                    text: "内存测试"
                                    icon: "memory-stick"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("内存测试")
                                }
                            }

                            Item { Layout.fillHeight: true }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: "下载前校验镜像"
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
                        Layout.preferredHeight: 136
                        title: "BROM 状态"
                        icon: "cpu"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "BROM"
                                detail: "未连接"
                            }
                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "DA / FDL"
                                detail: "未加载"
                            }
                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: App.adbDevices.length > 0 ? "ok" : "idle"
                                label: "ADB 设备"
                                detail: App.adbDevices.length > 0 ? ("已连接 " + App.adbDevices.length) : "未连接"
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                Layout.fillWidth: true
                                text: "端口 COM — · 波特率 460800"
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
                            { level: "info", message: "SP Flash Tool 工作台已就绪，等待加载 scatter 分区表" }
                        ]
                    }
                }
            }
        }
    }

    FileDialog {
        id: scatterDialog
        title: "选择 scatter 分区表"
        nameFilters: [
            "MTK scatter (*.txt *.scatter *.xml)",
            "所有文件 (*.*)"
        ]
        onAccepted: page.loadScatter(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }
}
