import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import XMF

// 内置模式 · RKDevTool 工作台（瑞芯微 Rockchip）
// 固件容器解析尚未接入后端：未解析时显示空状态，仅支持刷新已连接设备的 ADB 分区。
Item {
    id: page

    signal backRequested()

    readonly property color modeColor: Theme.accent

    property bool rebootAfterFlash: true
    property string tableSubtitle: ""

    function pickFirmware(path) {
        if (path.length === 0) {
            firmwareDialog.open()
            return
        }
        const result = App.workspace.loadRkFirmware(path)
        if (!result.ok) {
            App.notify("error", result.error || "RK 固件解析失败")
            logBox.append("error", result.error || "RK 固件解析失败")
            return
        }
        frame.fwPath = path
        frame.fwLoaded = true
        frame.statusKind = "ready"
        frame.statusText = "RK 固件已解析 · " + (result.subtitle || "")
        frame.progress = 0
        page.tableSubtitle = result.subtitle || ""
        table.setPartitions(result.partitions || [])
        logBox.append("success", "RK 固件：" + (result.subtitle || ""))
        App.notify("success", "RK 固件解析完成：" + (result.partitions || []).length + " 个分区")
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
        frame.statusText = "已读取设备分区"
        frame.statusKind = "ready"
        logBox.append("success", "设备分区：" + page.tableSubtitle)
    }

    function runAction(name) {
        if (table.count === 0) {
            App.notify("warning", "请先读取设备分区；RK 固件容器解析将在后续接入")
            return
        }
        logBox.append("info", "触发操作：" + name)
        App.notify("info", "RK 刷写：请先将设备切到 MaskROM / Loader 模式，并在工具库中使用 RK 流水线（读取分区后即可备份/写入单分区）")
    }

    ModeFrame {
        id: frame
        anchors.fill: parent

        title: "RKDevTool"
        subtitle: "瑞芯微固件与分区镜像 · MaskROM / Loader 下载"
        icon: "box"
        tag: "ROCKUSB"
        accentColor: page.modeColor

        firmwareLabel: "RK 固件"
        firmwarePlaceholder: "选择 RK 固件 / loader（*.img）"
        fwPath: ""
        fwLoaded: false

        progress: 0
        statusText: "就绪 · 未解析固件"
        statusKind: "idle"
        deviceText: App.adbDevices.length > 0 ? "已连接设备" : "未检测到设备"
        deviceKind: App.adbDevices.length > 0 ? "ok" : "idle"
        speedText: "—"
        deviceCode: App.adbDevices.length > 0 ? String(App.adbDevices[0].device || App.adbDevices[0].model || App.adbDevices[0].serial || "—") : "—"
        bootloaderText: "未知"
        slotText: "—"

        onBackRequested: page.backRequested()
        onBrowseRequested: firmwareDialog.open()
        onLoadRequested: page.pickFirmware(frame.fwPath)
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
                onClicked: page.runAction("下载固件")
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: 12

            // ---- 分区列表
            ModeCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "分区列表"
                subtitle: page.tableSubtitle.length > 0 ? page.tableSubtitle : "尚未解析固件"
                icon: "hard-drive"
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
                    emptyTitle: "尚未解析 RK 固件"
                    emptyText: "RK 固件容器解析将在后续接入；现在可刷新已连接设备，读取 ADB 分区列表。"
                    emptyActionText: "刷新设备分区"
                    onEmptyAction: page.scanDevice()
                    onRowAction: function (index) {
                        const row = table.rowAt(index)
                        page.runAction("刷写 " + (row ? row.name : ""))
                    }
                }
            }

            // ---- 右侧：设备状态 + 操作 + 日志
            Item {
                Layout.preferredWidth: 330
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    ModeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 136
                        title: "设备状态"
                        icon: "cpu"
                        accentColor: page.modeColor

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "MaskROM"
                                detail: "未连接"
                            }
                            ModeStatusLamp {
                                Layout.fillWidth: true
                                stateName: "idle"
                                label: "Loader"
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
                        Layout.preferredHeight: 172
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
                                    text: "下载"
                                    icon: "download"
                                    kind: 0
                                    small: true
                                    onClicked: page.runAction("下载固件")
                                }
                                AppButton {
                                    Layout.fillWidth: true
                                    text: "烧录"
                                    icon: "zap"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("烧录分区")
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
                                    text: "读回"
                                    icon: "upload"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("读回分区")
                                }
                                AppButton {
                                    Layout.fillWidth: true
                                    text: "擦除"
                                    icon: "trash-2"
                                    kind: 1
                                    small: true
                                    onClicked: page.runAction("擦除分区")
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
                                    text: "烧录完成后自动重启"
                                    color: Theme.textSub
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                                ToggleSwitch {
                                    Layout.alignment: Qt.AlignVCenter
                                    checked: page.rebootAfterFlash
                                    accentColor: Theme.textMuted
                                    onToggled: function (value) { page.rebootAfterFlash = value }
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
                            { level: "info", message: "RKDevTool 工作台已就绪；固件容器解析将在后续接入" }
                        ]
                    }
                }
            }
        }
    }

    FileDialog {
        id: firmwareDialog
        title: "选择 RK 固件"
        nameFilters: [
            "RK 固件 (*.img *.bin *.update)",
            "所有文件 (*.*)"
        ]
        onAccepted: page.pickFirmware(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }
}
