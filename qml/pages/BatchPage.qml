import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// Batch page: queue the same tool over a list of files (or a whole folder) and
// run it serially through the normal task engine.
Item {
    id: page

    property var files: []          // [{path,name,sizeText}]
    property int toolIndex: -1
    property var batchTools: []
    property int parallel: 2

    readonly property var batchState: App.batchState
    property bool running: batchState.active === true

    Component.onCompleted: refreshTools()

    function refreshTools() {
        const out = []
        const tools = App.tools
        for (let i = 0; i < tools.length; ++i) {
            const fields = tools[i].fields || []
            for (let j = 0; j < fields.length; ++j) {
                if (fields[j].type === "path" || fields[j].type === "paths") {
                    out.push(tools[i])
                    break
                }
            }
        }
        batchTools = out
        if (toolIndex < 0 && out.length > 0)
            toolIndex = 0
    }

    function pathFieldOf(tool) {
        const fields = (tool && tool.fields) || []
        for (let i = 0; i < fields.length; ++i)
            if (fields[i].type === "path" || fields[i].type === "paths")
                return fields[i]
        return null
    }

    function addFiles(list) {
        const next = files.slice()
        for (let i = 0; i < list.length; ++i) {
            let exists = false
            for (let j = 0; j < next.length; ++j)
                if (next[j].path === list[i].path) { exists = true; break }
            if (!exists)
                next.push(list[i])
        }
        files = next
    }

    function start() {
        const tool = batchTools[toolIndex]
        const field = pathFieldOf(tool)
        if (!tool || !field) {
            App.notify("warning", "请选择带文件参数的工具")
            return
        }
        if (files.length === 0) {
            App.notify("warning", "请先添加要处理的文件")
            return
        }
        const paths = []
        for (let i = 0; i < files.length; ++i)
            paths.push(files[i].path)
        App.startBatch(tool.id, field.key, paths, ({}), page.parallel)
    }

    function rowState(index) {
        if (index < batchState.done)
            return "完成"
        if (batchState.active && index === batchState.done)
            return "执行中"
        return "等待"
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 6

        PageHeader {
            width: parent.width
            title: "批量任务"
            subtitle: "同一工具按文件排队执行 · " + files.length + " 个文件"
            icon: "list-checks"
            AppButton { text: "添加文件"; kind: 1; small: true; onClicked: fileDialog.open() }
            AppButton { text: "添加文件夹"; kind: 1; small: true; onClicked: folderDialog.open() }
            AppButton {
                text: "清空列表"
                kind: 1
                small: true
                enabled: !page.running
                onClicked: page.files = []
            }
        }

        // tool selector
        GlassSurface {
            width: parent.width
            height: 44
            Column {
                anchors.fill: parent
                anchors.margins: 7
                Row {
                    width: parent.width
                    spacing: 8
                    Text {
                        text: "执行工具"
                        color: Theme.textSub
                        font.pixelSize: 11
                        width: 56
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        id: toolCombo
                        width: 320
                        height: 26
                        model: page.batchTools
                        textRole: "name"
                        currentIndex: page.toolIndex
                        onActivated: page.toolIndex = index
                        font.pixelSize: 11
                        background: Rectangle {
                            radius: Theme.r10
                            color: Theme.panel
                            border.width: 1
                            border.color: Theme.border
                        }
                        indicator: IconImage { x: toolCombo.width - 18; y: 7; name: "chevron-down"; size: 12; tint: Theme.textMuted }
                        contentItem: Text {
                            leftPadding: 6
                            text: toolCombo.displayText
                            color: Theme.text
                            font.pixelSize: 11
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Text {
                        text: page.pathFieldOf(page.batchTools[page.toolIndex])
                              ? "文件参数：" + page.pathFieldOf(page.batchTools[page.toolIndex]).label
                              : "该工具没有文件参数"
                        color: Theme.textMuted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item { width: Math.max(0, parent.width - 700); height: 1 }
                    Text {
                        text: "并发"
                        color: Theme.textMuted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        id: parallelCombo
                        width: 64
                        height: 26
                        model: [1, 2, 3, 4]
                        currentIndex: page.parallel - 1
                        onActivated: page.parallel = model[index]
                        font.pixelSize: 11
                        background: Rectangle {
                            radius: Theme.r10
                            color: Theme.panel
                            border.width: 1
                            border.color: Theme.border
                        }
                        indicator: IconImage { x: parallelCombo.width - 16; y: 7; name: "chevron-down"; size: 10; tint: Theme.textMuted }
                        contentItem: Text {
                            leftPadding: 6
                            text: parallelCombo.displayText
                            color: Theme.text
                            font.pixelSize: 11
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        // progress
        GlassSurface {
            width: parent.width
            height: 54
            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                Row {
                    width: parent.width
                    Text {
                        text: page.batchState.active
                              ? ("进行中 " + page.batchState.done + " / " + page.batchState.total
                                 + " · 运行 " + (page.batchState.running || 0)
                                 + " · 失败 " + page.batchState.failed)
                              : (page.batchState.total > 0
                                 ? ("已结束 成功 " + (page.batchState.done - page.batchState.failed)
                                    + " 失败 " + page.batchState.failed)
                                 : "尚未开始")
                        color: Theme.text
                        font.pixelSize: 11
                    }
                    Item { width: parent.width - 430; height: 1 }
                    Text {
                        visible: page.batchState.total > 0
                        text: page.batchState.percent + "%"
                        color: Theme.textSub
                        font.pixelSize: 11
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 6
                    radius: 2
                    color: Theme.divider
                    Rectangle {
                        width: parent.width * (page.batchState.percent || 0) / 100
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                    }
                }
                Text {
                    text: page.batchState.current || "空闲"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }

        Row {
            width: parent.width
            spacing: 6
            AppButton {
                text: "开始批量"
                kind: 0
                enabled: !page.running && page.files.length > 0 && page.batchTools.length > 0
                onClicked: page.start()
            }
            AppButton {
                text: "取消"
                kind: 1
                enabled: page.running
                onClicked: App.cancelBatch()
            }
        }

        // file table
        GlassSurface {
            width: parent.width
            height: parent.height - 44 - 54 - 30 - 6 - 6 - 6
            title: "文件列表"

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2
                Row {
                    width: parent.width
                    height: 20
                    Text { width: 30; text: "#"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: parent.width - 30 - 90 - 260 - 210; text: "文件"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: 90; text: "大小"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: 260; text: "路径"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: 110; text: "状态"; color: Theme.textMuted; font.pixelSize: 10 }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.divider }

                ListView {
                    width: parent.width
                    height: parent.height - 24
                    clip: true
                    model: page.files
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        width: ListView.view.width
                        height: 24
                        Rectangle { anchors.fill: parent; color: fMouse.containsMouse ? Theme.panelHover : "transparent" }
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            Text { width: 30; text: (index + 1); color: Theme.textMuted; font.pixelSize: 10 }
                            Text { width: parent.width - 30 - 90 - 260 - 210; text: modelData.name; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                            Text { width: 90; text: modelData.sizeText || ""; color: Theme.textSub; font.pixelSize: 11 }
                            Text { width: 260; text: modelData.path; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideMiddle }
                            Text {
                                width: 110
                                text: page.rowState(index)
                                color: page.rowState(index) === "执行中" ? Theme.accent
                                      : (page.rowState(index) === "完成" ? Theme.success : Theme.textMuted)
                                font.pixelSize: 11
                            }
                        }
                        MouseArea { id: fMouse; anchors.fill: parent; hoverEnabled: true }
                    }
                    EmptyState {
                        anchors.fill: parent
                        visible: page.files.length === 0
                        icon: "list-checks"
                        title: "列表为空"
                        text: "添加要批量处理的镜像 / 固件文件，或直接选择一个文件夹。"
                        actionText: "添加文件"
                        onAction: fileDialog.open()
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "添加文件（可多选）"
        nameFilters: ["镜像与固件 (*.img *.bin *.tgz *.tar.gz *.zip *.pac *.elf *.mbn *.xml *.txt)", "所有文件 (*.*)"]
        onAccepted: {
            const p = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            page.addFiles([{ path: p, name: p.split("\\").pop(), sizeText: "" }])
        }
    }

    FolderDialog {
        id: folderDialog
        title: "选择包含镜像/固件的文件夹"
        onAccepted: {
            const dir = String(selectedFolder).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            const list = App.filesInDirectory(dir, "*.img;*.bin;*.tgz;*.zip;*.pac;*.elf;*.mbn")
            page.addFiles(list)
        }
    }
}
