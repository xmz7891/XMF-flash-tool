import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// Resource packs: table of installed GeekFlashPacket packages plus a detail
// table of the models inside the selected package.
Item {
    id: page

    property string selectedPacket: ""
    property var packetList: App.packets.packets
    property var models: selectedPacket !== "" ? App.packets.models(selectedPacket) : []

    function selectPacket(id) {
        selectedPacket = (selectedPacket === id) ? "" : id
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 6

        PageHeader {
            width: parent.width
            title: "深度刷机引导"
            subtitle: App.packets.packets.length + " 个平台引导包 · " + App.packets.modelCount + " 个机型 · 高通 / 联发科 / 紫光展锐深度刷机 DA / Loader / FDL"
            icon: "package"
            AppButton { text: "打开目录"; kind: 1; small: true; onClicked: App.openPath(App.packets.packetDir) }
            AppButton { text: "重新扫描"; kind: 1; small: true; onClicked: App.packets.refresh() }
            AppButton { text: "导入引导包"; kind: 0; small: true; onClicked: importDialog.open() }
            AppButton {
                text: "引导文件"
                icon: "package-open"
                kind: 1
                small: true
                onClicked: guideOverlay.visible = true
            }
            AppButton {
                text: "导出机型清单"
                kind: 1
                small: true
                onClicked: packetSheet.open(App.toolInfo("packets.export"))
            }
            AppButton {
                text: "解析引导文件"
                kind: 1
                small: true
                onClicked: packetSheet.open(App.toolInfo("packets.resolve"))
            }
        }

        GlassSurface {
            width: parent.width
            height: 150
            title: "已安装引导包"

            ListView {
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: page.packetList
                delegate: Item {
                    width: ListView.view.width
                    height: 26
                    Rectangle {
                        anchors.fill: parent
                        color: page.selectedPacket === modelData.id ? Theme.selection
                              : (rowMouse.containsMouse ? Theme.panelHover : "transparent")
                    }
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        Text { width: 220; text: modelData.name; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                        Text { width: 60; text: modelData.type; color: Theme.textSub; font.pixelSize: 11 }
                        Text { width: 74; text: modelData.brandCount + " 品牌"; color: Theme.textSub; font.pixelSize: 11 }
                        Text { width: 74; text: modelData.seriesCount + " 系列"; color: Theme.textSub; font.pixelSize: 11 }
                        Text { width: 74; text: modelData.modelCount + " 机型"; color: Theme.text; font.pixelSize: 11 }
                        Text {
                            text: "v" + (modelData.version || "-") + " · " + (modelData.publisher || "未知")
                                  + " · " + (modelData.sourceType === "zip" ? "ZIP" : "目录")
                            color: Theme.textMuted
                            font.pixelSize: 10
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.selectPacket(modelData.id)
                    }
                }
                EmptyState {
                    anchors.fill: parent
                    visible: page.packetList.length === 0
                    icon: "package"
                    title: "尚未安装引导包"
                    text: "把 GeekFlashPacket 压缩包放入 data/packets，或点击「导入引导包」。"
                    actionText: "导入引导包"
                    onAction: importDialog.open()
                }
            }
        }

        GlassSurface {
            width: parent.width
            height: parent.height - 232
            title: page.selectedPacket === "" ? "机型列表（选择上方引导包）" : "机型列表 · " + page.models.length + " 条"

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                Row {
                    width: parent.width
                    height: 20
                    Text { width: 130; text: "品牌"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: 150; text: "系列"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { width: 240; text: "机型"; color: Theme.textMuted; font.pixelSize: 10 }
                    Text { text: "说明"; color: Theme.textMuted; font.pixelSize: 10 }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.divider }

                ListView {
                    width: parent.width
                    height: parent.height - 24
                    clip: true
                    model: page.models
                    spacing: 0
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        width: ListView.view.width
                        height: 24
                        Rectangle { anchors.fill: parent; color: mRow.containsMouse ? Theme.panelHover : "transparent" }
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            Text { width: 130; text: modelData.brand || ""; color: Theme.textSub; font.pixelSize: 11; elide: Text.ElideRight }
                            Text { width: 150; text: modelData.series || ""; color: Theme.textSub; font.pixelSize: 11; elide: Text.ElideRight }
                            Text { width: 240; text: modelData.name || ""; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                            Text { text: modelData.description || ""; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                        MouseArea { id: mRow; anchors.fill: parent; hoverEnabled: true }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    anchors.topMargin: 22
                    visible: page.selectedPacket === "" || page.models.length === 0
                    icon: "package"
                    title: page.packetList.length === 0 ? "没有引导包" : "选择引导包查看机型"
                    text: page.packetList.length === 0
                          ? "放入 data/packets 或点击右上角导入。"
                          : "点击上方引导包即可展开其中的品牌、系列、机型与引导文件。"
                }
            }
        }
    }

    ToolSheet { id: packetSheet; anchors.fill: parent }

    FileDialog {
        id: importDialog
        title: "选择 GeekFlashPacket 引导包"
        nameFilters: ["引导包 (*.zip)", "所有文件 (*.*)"]
        onAccepted: {
            const path = String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
            if (App.packets.importPackage(path))
                App.notify("success", "引导包导入成功")
            else
                App.notify("error", "引导包导入失败")
        }
    }

    // 引导文件选择：本地选择或从引导包提取（免授权 DA/Loader/FDL）
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
            width: Math.min(760, parent.width - 80)
            height: 190
            anchors.centerIn: parent
            title: "深度刷机引导文件（本地 / 引导包）"

            GuideFilePanel {
                anchors.fill: parent
                anchors.margins: 8
                platform: ""
                onUseRequested: function(path, attribute) {
                    App.notify("info", "已设为当前引导文件，可在 9008 / MTK / SPD 等工具中使用：" + path)
                }
            }

            Item {
                anchors.top: parent.top
                anchors.right: parent.right
                width: 30; height: 30
                IconImage { anchors.centerIn: parent; name: "x"; size: 12; tint: Theme.textSub }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: guideOverlay.visible = false }
            }
        }
    }

}
