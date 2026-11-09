import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// Guide/loader file selector for deep-flash workbenches.
// A guide file can come from the local disk or be extracted from one of the
// bundled platform packets (the "license-free" DA/Loader/FDL files).
Item {
    id: panel

    property string platform: "qcom"
    /// Optional explicit attribute list (packets page uses a combined list).
    property var attributeList: []
    property string title: "深度刷机引导文件（免授权）"
    property int packetIndex: 0
    property int modelIndex: 0
    property int attributeIndex: 0

    signal useRequested(string path, string attribute)

    implicitWidth: 640
    implicitHeight: 132

    readonly property var platformsAttributes: ({
        qcom: ["Loader", "Firehose", "DA", "DA1", "DA2"],
        mtk: ["DA1", "DA2", "Preloader"],
        sprd: ["FDL1", "FDL2"],
        rk: ["Loader", "Maskrom"],
        aw: ["FEL"],
        "": ["Loader", "DA1", "DA2", "FDL1", "FDL2", "Preloader", "Firehose", "FEL"]
    })

    readonly property var attributes: attributeList.length > 0
                                      ? attributeList
                                      : (platformsAttributes[platform] || platformsAttributes[""])
    readonly property var packets: App.packets.packets

    function platformPacketType() {
        if (platform === "mtk")
            return "MTK"
        if (platform === "sprd")
            return "SPRD"
        if (platform === "qcom")
            return "QC"
        return ""
    }

    function selectPlatformPacket() {
        const wanted = platformPacketType()
        if (wanted.length === 0)
            return
        for (let i = 0; i < packets.length; ++i) {
            if (String(packets[i].type || "").toUpperCase() === wanted) {
                packetIndex = i
                modelIndex = 0
                return
            }
        }
    }

    Component.onCompleted: selectPlatformPacket()
    onPlatformChanged: selectPlatformPacket()
    readonly property string currentPacket: packets.length > 0 && packetIndex < packets.length
                                            ? String(packets[packetIndex].id || "") : ""
    readonly property var models: currentPacket.length > 0 ? App.packets.models(currentPacket) : []

    function fileFilters() {
        if (platform === "mtk")
            return ["引导文件 (*.bin *.img *.elf)", "所有文件 (*.*)"]
        if (platform === "sprd")
            return ["引导文件 (*.bin *.fdl)", "所有文件 (*.*)"]
        if (platform === "rk")
            return ["Loader (*.bin *.img)", "所有文件 (*.*)"]
        if (platform === "aw")
            return ["引导文件 (*.bin *.img *.fex)", "所有文件 (*.*)"]
        return ["Firehose Loader (*.elf *.mbn *.bin *.devprg)", "所有文件 (*.*)"]
    }

    function useLocal(path) {
        App.guideFile = path
        App.notify("success", "已选择本地引导文件：" + path)
        panel.useRequested(path, "")
    }

    function extractFromPacket() {
        if (currentPacket.length === 0 || models.length === 0)
            return
        const model = models[Math.min(modelIndex, models.length - 1)]
        const modelName = String(model.name || "")
        const attribute = String(attributes[Math.min(attributeIndex, attributes.length - 1)])
        const path = App.pickGuideFromPacket(currentPacket, modelName, attribute)
        if (path.length > 0)
            panel.useRequested(path, attribute)
    }

    FileDialog {
        id: fileDialog
        title: "选择本地引导文件"
        nameFilters: panel.fileFilters()
        onAccepted: panel.useLocal(String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
    }

    GlassSurface {
        anchors.fill: parent
        title: panel.title

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            Row {
                width: parent.width
                height: 26
                spacing: 8
                Text {
                    text: "当前引导文件"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - 300
                    text: App.guideFile.length > 0 ? App.guideFile : "未选择（可本地选择，或从下方引导包提取）"
                    color: App.guideFile.length > 0 ? Theme.text : Theme.textMuted
                    font.pixelSize: 10
                    font.family: "Consolas"
                    elide: Text.ElideMiddle
                    anchors.verticalCenter: parent.verticalCenter
                }
                AppButton {
                    text: "选择本地文件"
                    icon: "folder-open"
                    kind: 1
                    small: true
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: fileDialog.open()
                }
                AppButton {
                    text: "复制路径"
                    icon: "copy"
                    kind: 2
                    small: true
                    visible: App.guideFile.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: App.copyText(App.guideFile)
                }
                AppButton {
                    text: "清除"
                    kind: 2
                    small: true
                    visible: App.guideFile.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: App.guideFile = ""
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            Row {
                width: parent.width
                height: 28
                spacing: 6
                Text {
                    text: "引导包"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: packetCombo
                    width: 190
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    model: panel.packets
                    textRole: "name"
                    currentIndex: panel.packetIndex
                    onActivated: { panel.packetIndex = index; panel.modelIndex = 0 }
                    font.pixelSize: 10
                    background: Rectangle { radius: Theme.r8; color: Theme.panelAlt; border.width: 1; border.color: Theme.border }
                    contentItem: Text {
                        leftPadding: 6
                        text: packetCombo.currentIndex >= 0
                              ? String(packetCombo.model[packetCombo.currentIndex].name || "") : "—"
                        color: Theme.text
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                Text {
                    text: "机型"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: modelCombo
                    width: 180
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    model: panel.models
                    textRole: "name"
                    currentIndex: panel.modelIndex
                    onActivated: panel.modelIndex = index
                    font.pixelSize: 10
                    background: Rectangle { radius: Theme.r8; color: Theme.panelAlt; border.width: 1; border.color: Theme.border }
                    contentItem: Text {
                        leftPadding: 6
                        text: modelCombo.currentIndex >= 0
                              ? String(modelCombo.model[modelCombo.currentIndex].name || "") : "—"
                        color: Theme.text
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                Text {
                    text: "文件"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: attrCombo
                    width: 120
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    model: panel.attributes
                    currentIndex: panel.attributeIndex
                    onActivated: panel.attributeIndex = index
                    font.pixelSize: 10
                    background: Rectangle { radius: Theme.r8; color: Theme.panelAlt; border.width: 1; border.color: Theme.border }
                    contentItem: Text {
                        leftPadding: 6
                        text: attrCombo.currentIndex >= 0 ? String(attrCombo.model[attrCombo.currentIndex]) : "—"
                        color: Theme.text
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                AppButton {
                    text: "从引导包提取"
                    icon: "package"
                    kind: 0
                    small: true
                    enabled: panel.currentPacket.length > 0 && panel.models.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: panel.extractFromPacket()
                }
            }
        }
    }
}
