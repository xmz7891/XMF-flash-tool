import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import XMF

Item {
    id: page
    signal backRequested()
    signal openPage(string key)

    readonly property color modeColor: Theme.accent
    property var partitions: []
    property var allPartitions: []
    property string tableSubtitle: ""
    property bool reading: false
    property bool lunCollapsed: false
    property string tableMode: "device"
    property int pendingRow: -1
    property string guideStorage: "AUTO"
    property string guideAuth: "NONE"
    property string guideModel: ""

    readonly property var edlPorts: {
        let out = []
        for (let i = 0; i < App.flashPorts.length; ++i) {
            const p = App.flashPorts[i]
            const t = (String(p.kind||"") + " " + String(p.description||"") + " "
                       + String(p.vidPid||"") + " " + String(p.chipLabel||"")).toLowerCase()
            if (t.indexOf("9008") >= 0 || t.indexOf("edl") >= 0
                    || t.indexOf("qualcomm") >= 0 || t.indexOf("05c6") >= 0)
                out.push(p)
        }
        return out
    }

    function openTool(toolId, extra) {
        const preset = { programmer: App.guideFile }
        if (page.guideStorage !== "AUTO") preset.memory = page.guideStorage.toLowerCase()
        if (extra) for (const k in extra) preset[k] = extra[k]
        toolSheet.open(App.toolInfo(toolId), preset)
    }

    function applyLunView() {
        table.setPartitions(page.lunCollapsed ? [] : page.allPartitions)
    }

    function setTableMode(mode) {
        page.tableMode = mode
        if (mode === "device") page.readTable()
        else if (mode === "xml") xmlDialog.open()
        else page.openTool("qcom.flow.raw")
    }

    function readTable() {
        if (App.guideFile.length === 0) { App.notify("warning", "请先选择 Loader"); loaderDialog.open(); return }
        reading = true
        frame.statusText = "正在读取 GPT…"; frame.statusKind = "busy"
        const memory = page.guideStorage === "AUTO" ? "" : page.guideStorage.toLowerCase()
        const result = App.workspace.loadQcomPartitions(App.guideFile, memory)
        reading = false
        if (!result || !result.ok) {
            logBox.append("error", result && result.error ? result.error : "读取 GPT 失败")
            frame.statusText = "读取失败"; frame.statusKind = "error"; return
        }
        partitions = result.partitions || []
        allPartitions = partitions
        lunCollapsed = false
        table.setPartitions(partitions)
        tableSubtitle = result.subtitle || ""
        frame.fwLoaded = true
        frame.statusText = "GPT 已读取 · " + partitions.length + " 个分区"
        frame.statusKind = "ready"
        logBox.append("success", "9008 GPT：" + tableSubtitle)
        App.notify("success", "已读取 " + partitions.length + " 个分区")
    }

    function loadXml(path) {
        const r = App.workspace.loadRawprogramXml(path)
        if (!r || !r.ok) { App.notify("error", r && r.error ? r.error : "XML 解析失败"); return }
        partitions = r.partitions || []
        allPartitions = partitions
        lunCollapsed = false
        table.setPartitions(partitions)
        tableSubtitle = r.subtitle || ""
        frame.fwLoaded = true
        frame.statusText = "XML 已加载 · " + partitions.length + " 个分区"
        logBox.append("success", "rawprogram XML：" + tableSubtitle)
    }

    function mapDirectory(dir) {
        const list = App.filesInDirectory(dir, "*.img;*.bin")
        let matched = 0
        for (let f = 0; f < list.length; ++f) {
            const file = list[f]
            const base = String(file.name || file.path).replace(/\.[^.]+$/, "")
            for (let i = 0; i < page.partitions.length; ++i) {
                const n = String(page.partitions[i].name)
                if (n === base || n === base.replace(/_[ab]$/, "")) {
                    table.setFile(i, file.path)
                    page.partitions[i].file = file.path
                    matched += 1
                    break
                }
            }
        }
        if (matched > 0) { logBox.append("success", "已匹配 " + matched + " 个镜像"); App.notify("success", "已匹配 " + matched + " 个分区") }
        else App.notify("warning", "目录中没有匹配的分区镜像")
    }

    function selectedNames() {
        const out = []
        for (let i = 0; i < partitions.length; ++i)
            if (partitions[i].checked) out.push(partitions[i].name)
        return out
    }

    function flashWithFiles() {
        const rows = page.partitions.filter(function(r) { return r.checked && String(r.file || "").length > 0 })
        if (rows.length === 0) { App.notify("warning", "请先勾选带镜像文件的分区"); return }
        for (const row of rows)
            App.runTool("qcom.flow.write", { programmer: App.guideFile,
                                             memory: page.guideStorage === "AUTO" ? "" : page.guideStorage.toLowerCase(),
                                             partition: row.name, input: row.file, confirm: true })
        logBox.append("info", "已提交 " + rows.length + " 个写入任务")
    }

    ModeFrame {
        id: frame
        anchors.fill: parent
        title: "Qualcomm 9008 深度刷机"
        subtitle: "Sahara + Firehose · GPT / 备份 / 恢复 / 写入"
        icon: "cpu"; tag: "EDL"; accentColor: page.modeColor
        firmwareLabel: "Loader"
        firmwarePlaceholder: "选择 Qualcomm Firehose programmer（.mbn / .elf / .bin）"
        fwPath: App.guideFile
        fwLoaded: App.guideFile.length > 0
        progress: page.reading ? 35 : (page.partitions.length > 0 ? 100 : 0)
        statusText: page.edlPorts.length > 0 ? (page.partitions.length > 0 ? "9008 就绪 · GPT 已读取" : "9008 已连接") : "未检测到 9008 端口"
        statusKind: page.edlPorts.length > 0 ? "ready" : "idle"
        deviceText: page.edlPorts.length > 0 ? "已连接" : "未连接"
        deviceKind: page.edlPorts.length > 0 ? "ok" : "idle"
        speedText: page.reading ? "读取中" : "—"
        deviceCode: page.edlPorts.length > 0 ? String(page.edlPorts[0].vidPid || page.edlPorts[0].portName || "9008") : "—"
        bootloaderText: "9008 / EDL"; slotText: "—"
        onBackRequested: page.backRequested()
        onBrowseRequested: loaderPicker.open()
        onLoadRequested: page.readTable()
        onReloadRequested: { App.scanPorts(); App.refreshDevices() }
        headerActions: [
            AppButton { text: "刷新端口"; icon: "refresh-cw"; kind: 1; small: true; onClicked: { App.scanPorts(); App.refreshDevices() } },
            AppButton { text: "引导库"; icon: "package"; kind: 1; small: true; onClicked: loaderPicker.open() },
            AppButton { text: "加载 XML"; kind: 2; small: true; onClicked: xmlDialog.open() },
            AppButton { text: "选择镜像目录"; icon: "folder-open"; kind: 1; small: true; onClicked: imageDirDialog.open() },
            AppButton { text: "刷入选中文件"; icon: "upload"; kind: 0; small: true; enabled: page.partitions.length > 0; onClicked: page.flashWithFiles() },
            AppButton { text: "读取 GPT"; icon: "hard-drive"; kind: 0; small: true; enabled: !page.reading; onClicked: page.readTable() }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: 12
            ModeCard {
                Layout.fillWidth: true; Layout.fillHeight: true
                title: "GPT 分区表"; subtitle: page.tableSubtitle.length > 0 ? page.tableSubtitle : "尚未读取分区表"
                icon: "list-checks"; accentColor: page.modeColor
                actions: [ Text { visible: table.count > 0; text: table.selectedCount + " / " + table.count; color: Theme.accent; font.pixelSize: Theme.fs(10); font.family: "Consolas" } ]
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 24; spacing: 6
                        AppButton { text: page.lunCollapsed ? "▶ LUN 0 (" + page.allPartitions.length + ")" : "▼ LUN 0 (" + page.allPartitions.length + ")"; kind: 2; small: true; onClicked: { page.lunCollapsed = !page.lunCollapsed; page.applyLunView() } }
                        Item { Layout.fillWidth: true }
                        AppButton { text: "设备分区表"; kind: page.tableMode === "device" ? 0 : 2; small: true; onClicked: page.setTableMode("device") }
                        AppButton { text: "XML"; kind: page.tableMode === "xml" ? 0 : 2; small: true; onClicked: page.setTableMode("xml") }
                        AppButton { text: "自定义地址"; kind: page.tableMode === "custom" ? 0 : 2; small: true; onClicked: page.setTableMode("custom") }
                    }
                    ModePartitionTable {
                        id: table
                        Layout.fillWidth: true; Layout.fillHeight: true
                        checkable: true
                        columns: [
                            { label: "LUN", role: "lun", width: 45, mono: true },
                            { label: "#", role: "index", width: 35, mono: true },
                            { label: "分区", role: "name", flex: true },
                            { label: "起始扇区", role: "startSector", width: 100, mono: true },
                            { label: "大小", role: "size", width: 80, align: "right", mono: true },
                            { label: "文件", role: "file", width: 120, mono: true },
                            { label: "操作", role: "action", width: 72 }
                        ]
                        rowActionText: "选择文件"
                        emptyIcon: "hard-drive"; emptyTitle: "尚未读取 9008 分区表"
                        emptyText: "连接 9008，选择 Loader，然后点击“读取 GPT”。"
                        emptyActionText: "读取 GPT"
                        onEmptyAction: page.readTable()
                        onSelectionChanged: page.partitions = table.partitions
                        onRowAction: function(index) { page.pendingRow = index; imageDialog.open() }
                    }
                }
            }

            Flickable {
                Layout.preferredWidth: Math.max(300, Math.min(430, page.width * 0.27))
                Layout.fillHeight: true
                contentHeight: rightColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ColumnLayout {
                    id: rightColumn; width: parent.width; spacing: 12
                    ModeCard {
                        Layout.fillWidth: true; Layout.preferredHeight: 320
                        title: "9008 操作"; icon: "flame"; accentColor: page.modeColor
                        ColumnLayout {
                            anchors.fill: parent; spacing: 6
                            AppButton { Layout.fillWidth: true; text: "备份选中分区"; icon: "download"; kind: 1; small: true; enabled: page.partitions.length > 0; onClicked: page.openTool("qcom.flow.backup", { partitions: page.selectedNames().join(",") }) }
                            AppButton { Layout.fillWidth: true; text: "恢复分区"; icon: "upload"; kind: 1; small: true; onClicked: page.openTool("qcom.flow.restore") }
                            AppButton { Layout.fillWidth: true; text: "写入选中分区"; icon: "flame"; kind: 0; small: true; enabled: page.partitions.length > 0; onClicked: page.openTool("qcom.flow.write", { partition: page.selectedNames().length > 0 ? page.selectedNames()[0] : "" }) }
                            AppButton { Layout.fillWidth: true; text: "擦除选中分区"; icon: "zap"; kind: 3; small: true; enabled: page.partitions.length > 0; onClicked: eraseMenu.popup() }
                            AppButton { Layout.fillWidth: true; text: "重启设备"; icon: "rotate-ccw"; kind: 2; small: true; onClicked: rebootMenu.popup() }
                            AppButton { Layout.fillWidth: true; text: "生成 rawprogram XML"; icon: "file-code"; kind: 1; small: true; enabled: page.partitions.length > 0; onClicked: page.openTool("qcom.partitions.xml") }
                            AppButton { Layout.fillWidth: true; text: "读取分区表 / 导出 CSV"; icon: "file-text"; kind: 2; small: true; onClicked: page.openTool("qcom.flow.partitions") }
                        }
                    }
                    ModeCard {
                        Layout.fillWidth: true; Layout.preferredHeight: 260
                        title: "更多功能"; icon: "wrench"; accentColor: page.modeColor
                        ColumnLayout {
                            anchors.fill: parent; spacing: 6
                            AppButton { Layout.fillWidth: true; text: "qedl 加载引导 / 读 GPT"; icon: "terminal"; kind: 0; small: true; onClicked: page.openTool("qcom.qedl.gpt") }
                            AppButton { Layout.fillWidth: true; text: "qedl 导出分区"; icon: "download"; kind: 1; small: true; onClicked: page.openTool("qcom.qedl.dump") }
                            AppButton { Layout.fillWidth: true; text: "脚本执行 / 命令控制台"; icon: "terminal"; kind: 1; small: true; onClicked: page.openPage("cmd-console") }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                AppButton { Layout.fillWidth: true; text: "槽位 A"; kind: 1; small: true; onClicked: page.openTool("qcom.flow.slot", { slot: "a" }) }
                                AppButton { Layout.fillWidth: true; text: "槽位 B"; kind: 1; small: true; onClicked: page.openTool("qcom.flow.slot", { slot: "b" }) }
                            }
                            AppButton { Layout.fillWidth: true; text: "偏移读写 / 自定义地址"; icon: "terminal"; kind: 2; small: true; onClicked: page.openTool("qcom.flow.raw") }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                AppButton { Layout.fillWidth: true; text: "Sahara 握手"; kind: 1; small: true; onClicked: page.openTool("qcom.sahara.handshake") }
                                AppButton { Layout.fillWidth: true; text: "存储信息"; kind: 1; small: true; onClicked: page.openTool("qcom.firehose.info") }
                            }
                        }
                    }
                    ModeLog { id: logBox; Layout.fillWidth: true; Layout.preferredHeight: 100 }
                }
            }
        }
    }

    Menu {
        id: eraseMenu
        MenuItem { text: "快速擦除（用户数据）"; onTriggered: page.openTool("qcom.flow.wipe", { mode: "quick", confirm: true }) }
        MenuItem { text: "完整擦除（保护关键分区）"; onTriggered: page.openTool("qcom.flow.wipe", { mode: "full", confirm: true }) }
    }
    Menu {
        id: rebootMenu
        MenuItem { text: "重启到系统"; onTriggered: page.openTool("qcom.flow.reboot", { target: "system" }) }
        MenuItem { text: "重启到 Fastboot"; onTriggered: page.openTool("qcom.flow.reboot", { target: "fastboot" }) }
        MenuItem { text: "重启到 Recovery"; onTriggered: page.openTool("qcom.flow.reboot", { target: "recovery" }) }
        MenuItem { text: "重启到 9008"; onTriggered: page.openTool("qcom.flow.reboot", { target: "9008" }) }
    }
    FileDialog {
        id: xmlDialog
        title: "选择 rawprogram XML"
        nameFilters: ["XML (*.xml)", "所有文件 (*.*)"]
        onAccepted: page.loadXml(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }
    FolderDialog {
        id: imageDirDialog
        title: "选择包含分区镜像的目录"
        onAccepted: page.mapDirectory(String(selectedFolder).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }
    FileDialog {
        id: imageDialog
        title: "选择该分区的镜像"
        nameFilters: ["镜像 (*.img *.bin *.raw)", "所有文件 (*.*)"]
        onAccepted: {
            const path = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            if (page.pendingRow >= 0) table.setFile(page.pendingRow, path)
        }
    }
    FileDialog {
        id: loaderDialog
        title: "选择 Qualcomm Firehose programmer"
        nameFilters: ["Loader (*.mbn *.elf *.bin *.devprg)", "所有文件 (*.*)"]
        onAccepted: {
            App.guideFile = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            frame.fwPath = App.guideFile
            frame.fwLoaded = true
        }
    }
    LoaderPicker {
        id: loaderPicker
        onLoaderChosen: function(path, storage, auth, model) {
            App.guideFile = path
            frame.fwPath = path
            frame.fwLoaded = true
            page.guideStorage = storage
            page.guideAuth = auth
            page.guideModel = model
            App.notify("success", "已选择 " + model + " · " + storage + " · " + auth)
        }
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }
}
