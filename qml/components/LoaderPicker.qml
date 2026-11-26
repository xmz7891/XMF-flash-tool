import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// 刷机匣式引导选择器：上方可选择本地引导文件；左侧厂商，中间机型；
// 右侧显示资源包信息。资源包机型会锁定存储类型/授权方案，本地文件则可编辑。
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 2000

    signal loaderChosen(string path, string storage, string auth, string model)

    property string brand: ""
    property string search: ""
    property var selectedModel: ({})
    property bool fromPacket: false
    property string localPath: ""
    property string storage: "AUTO"
    property string auth: "NONE"

    readonly property var storageOptions: ["AUTO", "EMMC", "UFS", "SPINOR", "NVME"]
    readonly property var authOptions: ["NONE", "Xiaomi", "Oneplus1", "Oneplus2", "Oneplus3", "OplusNew", "Nothing"]

    function open() {
        visible = true
        if (brand === "" && brands.length > 0) brand = brands[0]
    }
    function close() { visible = false }

    readonly property var qcModels: {
        const out = []
        const packets = App.packets.packets || []
        for (let i = 0; i < packets.length; ++i) {
            const p = packets[i]
            if (String(p.type || "").toUpperCase() !== "QC") continue
            const models = App.packets.models(p.id) || []
            for (let m = 0; m < models.length; ++m) {
                const item = models[m]
                item.packetId = p.id
                item.packetName = p.name
                item.publisher = p.publisher
                item.version = p.version
                item.published = p.published
                out.push(item)
            }
        }
        return out
    }

    readonly property var brands: {
        const seen = []
        for (let i = 0; i < qcModels.length; ++i) {
            const b = String(qcModels[i].brand || "未分类")
            if (seen.indexOf(b) < 0) seen.push(b)
        }
        seen.sort()
        return seen
    }

    readonly property var models: {
        const out = []
        const q = search.trim().toLowerCase()
        for (let i = 0; i < qcModels.length; ++i) {
            const m = qcModels[i]
            if (brand !== "" && String(m.brand || "未分类") !== brand) continue
            if (q !== "") {
                const hay = (String(m.name || "") + " " + String(m.series || "")
                             + " " + String(m.description || "")).toLowerCase()
                if (hay.indexOf(q) < 0) continue
            }
            out.push(m)
        }
        return out
    }

    function selectModel(m) {
        selectedModel = m
        fromPacket = true
        storage = String(m.Storage || "AUTO").toUpperCase()
        auth = String(m.Auth || "NONE")
    }

    function useSelected() {
        if (fromPacket) {
            if (!selectedModel.packetId) {
                App.notify("warning", "请先选择机型")
                return
            }
            const path = App.packets.modelResource(selectedModel.packetId,
                                                    String(selectedModel.name || ""), "Loader")
            if (!path) {
                App.notify("error", "资源包中没有解析出 Loader")
                return
            }
            loaderChosen(path, storage, auth, String(selectedModel.name || ""))
        } else {
            if (localPath.length === 0) {
                localDialog.open()
                return
            }
            loaderChosen(localPath, storage, auth, "本地引导文件")
        }
        close()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.darkMode ? "#AA000000" : "#77000000"
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    GlassSurface {
        width: Math.min(1000, parent.width - 60)
        height: Math.min(720, parent.height - 40)
        anchors.centerIn: parent
        title: "选择设备 / 引导 / 配置"

        Item {
            anchors.top: parent.top
            anchors.right: parent.right
            width: 34; height: 34
            IconImage { anchors.centerIn: parent; name: "x"; size: 13; tint: Theme.textSub }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.close() }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 12
            anchors.topMargin: 40
            spacing: 8

            // top: resource search
            Row {
                width: parent.width
                height: 34
                spacing: 8
                Rectangle {
                    width: 120; height: 30; radius: 8
                    color: Theme.selection
                    Text { anchors.centerIn: parent; text: "资源包"; color: Theme.accent; font.pixelSize: Theme.fs(11) }
                }
                SearchField {
                    width: 230
                    placeholder: "搜索机型 / 系列"
                    onTextChanged: root.search = text
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.qcModels.length + " 个高通机型"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fs(10)
                }
            }

            // local loader card (top area)
            Rectangle {
                width: parent.width
                height: 52
                radius: Theme.r12
                color: Theme.panelAlt
                border.width: 1
                border.color: root.fromPacket ? Theme.border : Theme.alpha(Theme.accent, 0.65)

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 10
                    IconImage {
                        name: "folder-open"
                        size: 18
                        tint: root.fromPacket ? Theme.textSub : Theme.accent
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Column {
                        width: parent.width - 190
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: "本地引导文件"
                            color: root.fromPacket ? Theme.textSub : Theme.accent
                            font.pixelSize: Theme.fs(11)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: root.localPath.length > 0 ? root.localPath : "选择 .mbn / .elf / .bin / .devprg；存储类型与授权方案可手动修改"
                            color: root.localPath.length > 0 ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fs(9)
                            elide: Text.ElideMiddle
                        }
                    }
                    AppButton {
                        text: "选择文件"
                        icon: "folder-open"
                        kind: 0
                        small: true
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: localDialog.open()
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            Row {
                width: parent.width
                height: parent.height - 96
                spacing: 10

                // brands
                Column {
                    width: 160; height: parent.height
                    Text { text: "厂商"; color: Theme.textMuted; font.pixelSize: Theme.fs(10); height: 20 }
                    ListView {
                        width: parent.width
                        height: parent.height - 22
                        clip: true
                        model: root.brands
                        spacing: 2
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 32
                            radius: Theme.r8
                            color: root.brand === modelData ? Theme.alpha(Theme.accent, 0.16)
                                                            : (bm.containsMouse ? Theme.panelHover : "transparent")
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: root.brand === modelData ? Theme.text : Theme.textSub
                                font.pixelSize: Theme.fs(11)
                            }
                            MouseArea {
                                id: bm; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.brand = modelData; root.selectedModel = ({}); root.fromPacket = false }
                            }
                        }
                    }
                }

                // models
                Column {
                    width: 300; height: parent.height
                    Text { text: "机型 / 系列"; color: Theme.textMuted; font.pixelSize: Theme.fs(10); height: 20 }
                    ListView {
                        width: parent.width
                        height: parent.height - 22
                        clip: true
                        model: root.models
                        spacing: 2
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 42
                            radius: Theme.r8
                            color: (root.fromPacket && root.selectedModel === modelData)
                                   ? Theme.alpha(Theme.accent, 0.16)
                                   : (mm.containsMouse ? Theme.panelHover : "transparent")
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    text: modelData.name || "未命名"
                                    color: Theme.text
                                    font.pixelSize: Theme.fs(11)
                                }
                                Text {
                                    text: (modelData.series || "") + " · " + (modelData.Storage || "AUTO")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fs(9)
                                }
                            }
                            MouseArea {
                                id: mm; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectModel(modelData)
                            }
                        }
                    }
                }

                // detail
                Rectangle {
                    width: parent.width - 480
                    height: parent.height
                    radius: Theme.r12
                    color: Theme.panelAlt
                    border.width: 1
                    border.color: Theme.border

                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text { text: root.fromPacket ? "资源包" : "当前来源"; color: Theme.textMuted; font.pixelSize: Theme.fs(10) }
                        Text {
                            width: parent.width
                            text: root.fromPacket ? (root.selectedModel.packetName || "—")
                                                  : (root.localPath.length > 0 ? "本地引导文件" : "尚未选择")
                            color: Theme.text
                            font.pixelSize: Theme.fs(13)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: root.fromPacket
                                  ? (root.selectedModel.publisher || "-") + " · v" + (root.selectedModel.version || "-")
                                  : "自定义 Loader，可配置存储类型和授权方案"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fs(10)
                            elide: Text.ElideRight
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.divider }

                        Text { text: "存储类型"; color: Theme.textMuted; font.pixelSize: Theme.fs(10) }
                        ComboBox {
                            id: storageCombo
                            width: parent.width; height: 30
                            model: root.storageOptions
                            currentIndex: Math.max(0, root.storageOptions.indexOf(root.storage))
                            enabled: !root.fromPacket
                            opacity: root.fromPacket ? 0.45 : 1
                            onActivated: root.storage = root.storageOptions[index]
                            background: Rectangle {
                                radius: Theme.r8
                                color: Theme.panel
                                border.width: 1
                                border.color: root.fromPacket ? Theme.border : Theme.alpha(Theme.accent, 0.55)
                            }
                            contentItem: Text {
                                leftPadding: 8
                                text: storageCombo.currentText
                                color: Theme.text
                                font.pixelSize: Theme.fs(11)
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Text { text: "授权方案"; color: Theme.textMuted; font.pixelSize: Theme.fs(10) }
                        ComboBox {
                            id: authCombo
                            width: parent.width; height: 30
                            model: root.authOptions
                            currentIndex: Math.max(0, root.authOptions.indexOf(root.auth))
                            enabled: !root.fromPacket
                            opacity: root.fromPacket ? 0.45 : 1
                            onActivated: root.auth = root.authOptions[index]
                            background: Rectangle {
                                radius: Theme.r8
                                color: Theme.panel
                                border.width: 1
                                border.color: root.fromPacket ? Theme.border : Theme.alpha(Theme.accent, 0.55)
                            }
                            contentItem: Text {
                                leftPadding: 8
                                text: authCombo.currentText
                                color: Theme.text
                                font.pixelSize: Theme.fs(11)
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Text {
                            width: parent.width
                            text: root.fromPacket
                                  ? "资源包机型已锁定：存储类型 / 授权方案由资源包决定，不能修改。"
                                  : "本地文件：可自由修改存储类型和授权方案。"
                            color: root.fromPacket ? Theme.warning : Theme.success
                            font.pixelSize: Theme.fs(9)
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            text: "Loader   " + (root.fromPacket ? (root.selectedModel.Loader || "—") : (root.localPath || "—"))
                            color: Theme.textDim
                            font.pixelSize: Theme.fs(9)
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Item { width: 1; height: 2 }
                        AppButton {
                            width: parent.width
                            text: root.fromPacket ? "使用此引导" : (root.localPath.length > 0 ? "使用本地引导" : "选择本地文件")
                            icon: "check"
                            kind: 0
                            small: true
                            onClicked: root.useSelected()
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: localDialog
        title: "选择本地引导文件"
        nameFilters: ["引导文件 (*.mbn *.elf *.bin *.devprg)", "所有文件 (*.*)"]
        onAccepted: {
            root.localPath = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            root.selectedModel = ({})
            root.fromPacket = false
            root.storage = "AUTO"
            root.auth = "NONE"
        }
    }
}
