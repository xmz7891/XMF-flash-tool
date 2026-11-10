import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// Flashing console: single-screen workbench in the spirit of professional
// desktop flash tools. Partition table on the left, live log on the right,
// guide/firmware staging and action bar underneath.
Item {
    id: page

    signal openMode(string modeId)
    signal openPage(string key)

    property var rows: []
    property string search: ""
    property string selectedFile: ""
    property int pendingRow: -1
    property bool autoReboot: true
    property bool protectPartitions: false
    property bool keepData: false
    property bool readInfo: true
    property bool confirmErase: false

    readonly property var activeTask: App.activeTasks.length > 0 ? App.activeTasks[0] : null
    readonly property var selectedRows: rows.filter(function(r) { return r.selected })
    readonly property var filteredRows: rows.filter(function(r) {
        return search.length === 0 || String(r.name).toLowerCase().indexOf(search.toLowerCase()) >= 0
    })

    function updateRow(index, patch) {
        const copy = rows.slice()
        const merged = ({})
        for (const k in copy[index]) merged[k] = copy[index][k]
        for (const k in patch) merged[k] = patch[k]
        copy[index] = merged
        rows = copy
    }

    function toggleRow(index) {
        const copy = rows.slice()
        const row = ({})
        for (const k in copy[index]) row[k] = copy[index][k]
        row.selected = !row.selected
        copy[index] = row
        rows = copy
    }

    function serial() {
        if (App.fastbootDevices.length > 0)
            return String(App.fastbootDevices[0].serial || "")
        if (App.adbDevices.length > 0)
            return String(App.adbDevices[0].serial || "")
        return ""
    }

    function readPartitions() {
        selectedFile = ""
        let result = null
        if (App.fastbootDevices.length > 0)
            result = App.workspace.loadFastbootPartitions(serial())
        else if (App.adbDevices.length > 0)
            result = App.workspace.loadAdbPartitions(serial())
        else {
            App.notify("warning", "未连接设备：请让设备进入 Fastboot 或开启 ADB 调试")
            return
        }
        if (!result || !result.ok) {
            App.notify("error", result && result.error ? result.error : "读取分区表失败")
            return
        }
        const list = []
        for (const p of (result.partitions || [])) {
            list.push({
                name: String(p.name || ""),
                sizeText: String(p.sizeText || ""),
                address: String(p.address || ""),
                size: Number(p.size || 0),
                file: "",
                selected: false
            })
        }
        rows = list
        App.notify("success", "已读取 " + list.length + " 个分区")
    }

    function chooseImage(index) {
        pendingRow = index
        imgDialog.open()
    }

    function openTool(id) {
        toolSheet.open(App.toolInfo(id))
    }

    function writeSelected() {
        const chosen = rows.filter(function(r) { return r.selected && r.file.length > 0 })
        if (chosen.length === 0) {
            App.notify("warning", "请先勾选分区，并双击「文件路径」选择要写入的镜像")
            return
        }
        if (App.fastbootDevices.length === 0) {
            App.notify("warning", "写入分区需要 Fastboot 模式设备；9008/Recovery 请使用对应工作台")
            return
        }
        for (const row of chosen) {
            App.runTool("fastboot.flash", ({
                serial: serial(),
                partition: row.name,
                image: row.file
            }))
        }
        App.notify("info", "已提交 " + chosen.length + " 个分区的刷写任务")
    }

    function eraseSelected() {
        const chosen = rows.filter(function(r) { return r.selected })
        if (chosen.length === 0) {
            App.notify("warning", "请先勾选要擦除的分区")
            return
        }
        eraseConfirm.visible = true
    }

    function doErase() {
        eraseConfirm.visible = false
        if (App.fastbootDevices.length === 0) {
            App.notify("warning", "擦除分区需要 Fastboot 模式设备")
            return
        }
        for (const row of rows.filter(function(r) { return r.selected }))
            App.runTool("fastboot.erase", ({ serial: serial(), partition: row.name, confirm: true }))
        App.notify("info", "已提交擦除任务")
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 6

        Row {
            width: parent.width
            height: parent.height - 220
            spacing: 0

            // ------------------------------------------------ partition area
            GlassSurface {
                width: parent.width - 336
                height: parent.height
                radius: 0
                title: "分区表"
                hint: page.rows.length > 0 ? page.rows.length + " 个分区 · " + page.selectedRows.length + " 已选" : "未读取"

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Row {
                        width: parent.width
                        height: 28
                        spacing: 6

                        Rectangle {
                            width: parent.width - 180
                            height: 28
                            radius: Theme.r6
                            color: Theme.panelAlt
                            border.width: 1
                            border.color: Theme.borderStrong
                            TextField {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                placeholderText: "请输入分区名称筛选…"
                                color: Theme.text
                                font.pixelSize: 11
                                background: null
                                selectByMouse: true
                                onTextChanged: page.search = text
                            }
                        }
                        AppButton {
                            text: "读分区表"
                            kind: 1
                            small: true
                            onClicked: page.readPartitions()
                        }
                        AppButton {
                            text: "全部取消"
                            kind: 2
                            small: true
                            onClicked: {
                                const copy = page.rows.slice()
                                for (let i = 0; i < copy.length; ++i) {
                                    const r = ({})
                                    for (const k in copy[i]) r[k] = copy[i][k]
                                    r.selected = false
                                    copy[i] = r
                                }
                                page.rows = copy
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.divider }

                    Row {
                        width: parent.width
                        height: 20
                        Text { width: 26; text: ""; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.30; text: "分区"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.15; text: "大小"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.15; text: "操作"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: parent.width * 0.36; text: "文件路径（双击选择镜像）"; color: Theme.textMuted; font.pixelSize: 10 }
                    }

                    Item {
                        width: parent.width
                        height: parent.height - 66

                        Column {
                            visible: page.rows.length === 0
                            width: parent.width
                            Repeater {
                                model: 12
                                delegate: Rectangle {
                                    width: parent ? parent.width : 0
                                    height: 26
                                    color: index % 2 === 1 ? Theme.alpha(Theme.panelAlt, 0.35) : "transparent"
                                }
                            }
                        }

                        ListView {
                            anchors.fill: parent
                            clip: true
                            model: page.filteredRows
                            spacing: 0
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Item {
                                width: ListView.view.width
                                height: 26
                                Rectangle {
                                    anchors.fill: parent
                                    color: rowSel.containsMouse ? Theme.panelHover : (modelData.selected ? Theme.accentSoft : "transparent")
                                }
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
                                    Rectangle {
                                        width: 14; height: 14
                                        radius: 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: modelData.selected ? Theme.accent : "transparent"
                                        border.width: 1
                                        border.color: modelData.selected ? Theme.accent : Theme.borderStrong
                                        IconImage {
                                            anchors.centerIn: parent
                                            visible: modelData.selected
                                            name: "check"
                                            size: 10
                                            tint: Theme.textOnAccent
                                        }
                                    }
                                    Text {
                                        width: parent.width * 0.30 - 18
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                        leftPadding: 6
                                    }
                                    Text {
                                        width: parent.width * 0.15
                                        text: modelData.sizeText
                                        color: Theme.textSub
                                        font.pixelSize: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: parent.width * 0.15
                                        text: modelData.file.length > 0 ? "已选择" : "选择…"
                                        color: modelData.file.length > 0 ? Theme.success : Theme.accent
                                        font.pixelSize: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: parent.width * 0.36
                                        text: modelData.file.length > 0 ? modelData.file : "双击选择镜像"
                                        color: modelData.file.length > 0 ? Theme.text : Theme.textMuted
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                MouseArea {
                                    id: rowSel
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.toggleRow(page.rows.indexOf(modelData))
                                    onDoubleClicked: page.chooseImage(page.rows.indexOf(modelData))
                                }
                            }
                        }

                        Row {
                            visible: page.rows.length === 0
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: 10
                            spacing: 10

                            IconImage {
                                name: "usb"
                                size: 16
                                tint: Theme.textMuted
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Column {
                                spacing: 1
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    text: "未读取分区表"
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: "连接 Fastboot 或开启 ADB 调试后读取；也可把固件 / 镜像拖入本页。"
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }
                            Item { width: Math.max(0, parent.width - 620); height: 1 }
                            AppButton {
                                text: "读分区表"
                                kind: 1
                                small: true
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: page.readPartitions()
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------------- log panel
            GlassSurface {
                width: 330
                height: parent.height
                radius: 0
                title: "执行日志"
                hint: logList.count + " 条"

                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4

                    ListView {
                        id: logList
                        width: parent.width
                        height: parent.height - 34
                        clip: true
                        model: App.logs
                        spacing: 1
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        onCountChanged: positionViewAtEnd()

                        delegate: Row {
                            width: logList.width
                            height: 20
                            spacing: 6
                            Text {
                                width: 62
                                text: model.time
                                color: Theme.textMuted
                                font.pixelSize: 9
                                font.family: "Consolas"
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width - 70
                                text: model.message
                                color: model.level >= 3 ? Theme.danger : (model.level === 2 ? Theme.warning : Theme.textSub)
                                font.pixelSize: 10
                                font.family: "Consolas"
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: 28
                        spacing: 6
                        AppButton {
                            text: "保存日志"
                            kind: 1
                            small: true
                            onClicked: saveLogDialog.open()
                        }
                        AppButton {
                            text: "清空"
                            kind: 2
                            small: true
                            onClicked: App.clearLogs()
                        }

                    }
                }
            }
        }

        // ------------------------------------------------ guide / firmware
        GlassSurface {
            width: parent.width
            height: 140
            title: "引导与固件"
            hint: "免授权引导文件 / 线刷包"

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Row {
                    width: parent.width
                    height: 26
                    spacing: 6
                    Text { width: 70; text: "引导文件"; color: Theme.textMuted; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle {
                        width: parent.width - 330
                        height: 26
                        radius: Theme.r6
                        color: Theme.panelAlt
                        border.width: 1
                        border.color: Theme.borderStrong
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            text: App.guideFile.length > 0 ? App.guideFile : "未选择（本地文件或引导包提取）"
                            color: App.guideFile.length > 0 ? Theme.text : Theme.textMuted
                            font.pixelSize: 10
                            font.family: "Consolas"
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    AppButton { text: "浏览"; kind: 1; small: true; onClicked: guideDialog.open() }
                    AppButton { text: "从引导包提取"; kind: 1; small: true; onClicked: guideOverlay.visible = true }
                }

                Row {
                    width: parent.width
                    height: 26
                    spacing: 6
                    Text { width: 70; text: "线刷包"; color: Theme.textMuted; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle {
                        width: parent.width - 330
                        height: 26
                        radius: Theme.r6
                        color: Theme.panelAlt
                        border.width: 1
                        border.color: Theme.borderStrong
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            text: page.selectedFile.length > 0 ? page.selectedFile : "选择 tgz / pac / img，或直接拖入窗口"
                            color: page.selectedFile.length > 0 ? Theme.text : Theme.textMuted
                            font.pixelSize: 10
                            font.family: "Consolas"
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    AppButton { text: "浏览"; kind: 1; small: true; onClicked: firmwareDialog.open() }
                    AppButton {
                        text: "一键刷写"
                        kind: 0
                        small: true
                        onClicked: page.openMode("miflash")
                    }
                }

                Row {
                    width: parent.width
                    height: 24
                    spacing: 16
                    AppCheckBox { text: "自动重启"; checked: page.autoReboot; onToggled: page.autoReboot = checked }
                    AppCheckBox { text: "保护分区"; checked: page.protectPartitions; onToggled: page.protectPartitions = checked }
                    AppCheckBox { text: "保数据"; checked: page.keepData; onToggled: page.keepData = checked }
                    AppCheckBox { text: "读取信息"; checked: page.readInfo; onToggled: page.readInfo = checked }
                    Item { width: Math.max(0, parent.width - 560); height: 1 }
                    AppButton {
                        text: "打开工具库"
                        kind: 2
                        small: true
                        onClicked: page.openPage("tools")
                    }
                }
            }
        }

        // -------------------------------------------------------- action bar
        Row {
            width: parent.width
            height: 34
            spacing: 6
            AppButton { text: "读分区表"; kind: 1; small: true; onClicked: page.readPartitions() }
            AppButton { text: "读取分区"; kind: 1; small: true; onClicked: page.openTool("adb.pull") }
            AppButton { text: "擦除分区"; kind: 3; small: true; onClicked: page.eraseSelected() }
            AppButton { text: "写入分区"; kind: 0; small: true; onClicked: page.writeSelected() }
            Item { width: Math.max(0, parent.width - 520); height: 1 }
            Text {
                text: page.activeTask ? (page.activeTask.title + " · " + page.activeTask.progress + "%") : "当前操作：无"
                color: Theme.textMuted
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 220
                elide: Text.ElideRight
            }
            AppButton {
                text: "停止操作"
                kind: 2
                small: true
                enabled: App.activeTasks.length > 0
                onClicked: App.cancelAllTasks()
            }
        }

        // ---------------------------------------------------------- progress
        Row {
            width: parent.width
            height: 18
            spacing: 8
            Rectangle {
                width: parent.width - 210
                height: 8
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.panelAlt
                border.width: 1
                border.color: Theme.border
                Rectangle {
                    width: page.activeTask ? parent.width * page.activeTask.progress / 100 : 0
                    height: parent.height
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Theme.durNormal; easing.type: Easing.OutCubic } }
                }
            }
            Text {
                text: page.activeTask ? page.activeTask.status : "就绪"
                color: Theme.textMuted
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 110
                elide: Text.ElideRight
            }
            Text {
                text: "0.0 MB/s"
                color: Theme.textMuted
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ------------------------------------------------------------ dialogs
    FileDialog {
        id: imgDialog
        title: "选择要写入的镜像"
        nameFilters: ["镜像文件 (*.img *.bin *.raw)", "所有文件 (*.*)"]
        onAccepted: {
            const path = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            if (page.pendingRow >= 0 && page.pendingRow < page.rows.length)
                page.updateRow(page.pendingRow, { file: path })
        }
    }

    FileDialog {
        id: firmwareDialog
        title: "选择线刷包 / 固件"
        nameFilters: ["线刷包 (*.tgz *.tar.gz *.pac *.zip)", "所有文件 (*.*)"]
        onAccepted: page.selectedFile = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
    }

    FileDialog {
        id: guideDialog
        title: "选择引导文件"
        nameFilters: ["引导文件 (*.elf *.mbn *.bin *.fdl *.devprg)", "所有文件 (*.*)"]
        onAccepted: App.guideFile = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
    }

    FileDialog {
        id: saveLogDialog
        title: "保存日志"
        nameFilters: ["日志文件 (*.txt)"]
        onAccepted: App.saveLogs(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }

    Item {
        id: guideOverlay
        anchors.fill: parent
        visible: false
        z: 900

        Rectangle {
            anchors.fill: parent
            color: Theme.darkMode ? "#99000000" : "#66000000"
            MouseArea { anchors.fill: parent; onClicked: guideOverlay.visible = false }
        }

        GlassSurface {
            width: Math.min(760, parent.width - 60)
            height: 170
            anchors.centerIn: parent
            title: "从引导包提取（免授权引导文件）"

            GuideFilePanel {
                anchors.fill: parent
                anchors.margins: 8
                platform: ""
                onUseRequested: function(path, attribute) {
                    guideOverlay.visible = false
                    App.notify("info", "已选择引导文件：" + path)
                }
            }

            Item {
                anchors.top: parent.top
                anchors.right: parent.right
                width: 30; height: 30
                IconImage { anchors.centerIn: parent; name: "x"; size: 12; tint: Theme.textMuted }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: guideOverlay.visible = false }
            }
        }
    }

    Item {
        id: eraseConfirm
        anchors.fill: parent
        visible: false
        z: 950

        Rectangle {
            anchors.fill: parent
            color: Theme.darkMode ? "#99000000" : "#66000000"
            MouseArea { anchors.fill: parent; onClicked: eraseConfirm.visible = false }
        }

        GlassSurface {
            width: 420
            height: 150
            anchors.centerIn: parent
            title: "确认擦除分区"

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Text {
                    width: parent.width
                    text: "将擦除已勾选的 " + page.selectedRows.length + " 个分区，数据无法恢复。"
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Row {
                    spacing: 8
                    AppButton { text: "取消"; kind: 1; small: true; onClicked: eraseConfirm.visible = false }
                    AppButton { text: "确认擦除"; kind: 3; small: true; onClicked: page.doErase() }
                }
            }
        }
    }

    ToolSheet { id: toolSheet; anchors.fill: parent }

    DropArea {
        anchors.fill: parent
        keys: ["urls"]
        onDropped: function(drop) {
            if (!drop.hasUrls)
                return
            const path = String(drop.urls[0]).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            page.selectedFile = path
            if (path.toLowerCase().endsWith(".img") || path.toLowerCase().endsWith(".bin")) {
                const result = App.workspace.loadGptImage(path)
                if (result && result.ok && result.partitions.length > 0) {
                    const list = []
                    for (const p of result.partitions)
                        list.push({ name: String(p.name || ""), sizeText: String(p.sizeText || ""),
                                    address: String(p.address || ""), size: Number(p.size || 0),
                                    file: "", selected: false })
                    page.rows = list
                }
            }
            drop.accept()
        }
    }
}
