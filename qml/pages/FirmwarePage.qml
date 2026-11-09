import QtQuick
import QtQuick.Controls.Basic
import XMF

// Firmware resources: offline brand/model database plus official download links
// and the online search/download tools.
Item {
    id: page

    property string brandId: App.firmware.brands.length > 0 ? App.firmware.brands[0].id : ""
    property string search: ""
    readonly property var brandList: App.firmware.brands
    readonly property var brandInfo: brandId === "" ? ({}) : App.firmware.brand(brandId)
    readonly property var models: brandId === "" ? [] : App.firmware.models(brandId, search, 400)

    function openFirstOfficial() {
        const links = page.brandInfo.official || []
        if (links.length > 0)
            App.openUrl(links[0].url)
        else
            App.notify("info", "该品牌没有内置官方入口")
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 6

        PageHeader {
            width: parent.width
            title: "固件资源"
            subtitle: "离线机型库 " + App.firmware.modelCount + " 条 · " + App.firmware.brands.length + " 个品牌 · 官方固件入口"
            icon: "download-cloud"
            SearchField {
                width: 220
                placeholder: "机型 / 代号 / 系列"
                onTextChanged: page.search = text
            }
            AppButton {
                text: "在线检索"
                kind: 0
                small: true
                onClicked: searchSheet.open(App.toolInfo("firmware.xiaomi.search"))
            }
            AppButton {
                text: "官方入口"
                kind: 1
                small: true
                onClicked: page.openFirstOfficial()
            }
            AppButton {
                text: "下载固件"
                kind: 1
                small: true
                onClicked: searchSheet.open(App.toolInfo("firmware.xiaomi.download"))
            }
            AppButton {
                text: "OTA 检查"
                kind: 1
                small: true
                onClicked: searchSheet.open(App.toolInfo("firmware.xiaomi.otaCheck"))
            }
            AppButton {
                text: "数据集"
                kind: 2
                small: true
                onClicked: searchSheet.open(App.toolInfo("firmware.dataset.info"))
            }
        }

        Row {
            width: parent.width
            height: parent.height - 46
            spacing: 8

            // brands
            GlassSurface {
                width: 220
                height: parent.height
                title: "品牌"

                ListView {
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: page.brandList
                    delegate: Item {
                        width: ListView.view.width
                        height: 26
                        Rectangle {
                            anchors.fill: parent
                            color: page.brandId === modelData.id ? Theme.selection
                                  : (bMouse.containsMouse ? Theme.panelHover : "transparent")
                        }
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            Text { text: modelData.name; color: Theme.text; font.pixelSize: 11; width: parent.width - 66; elide: Text.ElideRight }
                            Text { text: modelData.modelCount + " 机型"; color: Theme.textMuted; font.pixelSize: 10 }
                        }
                        MouseArea {
                            id: bMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.brandId = modelData.id
                        }
                    }
                }
            }

            // models
            GlassSurface {
                width: parent.width - 220 - 8
                height: parent.height
                title: (page.brandInfo.name || "机型列表") + " · " + page.models.length + " 条"

                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    Row {
                        width: parent.width
                        height: 20
                        Text { width: 140; text: "代号"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { width: 160; text: "系列"; color: Theme.textMuted; font.pixelSize: 10 }
                        Text { text: "机型名称"; color: Theme.textMuted; font.pixelSize: 10 }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.divider }
                    ListView {
                        width: parent.width
                        height: parent.height - 142
                        clip: true
                        model: page.models
                        spacing: 0
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Item {
                            width: ListView.view.width
                            height: 24
                            Rectangle { anchors.fill: parent; color: mMouse.containsMouse ? Theme.panelHover : "transparent" }
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                Text { width: 140; text: modelData.codename; color: Theme.textSub; font.pixelSize: 11; elide: Text.ElideRight }
                                Text { width: 160; text: modelData.series; color: Theme.textSub; font.pixelSize: 11; elide: Text.ElideRight }
                                Text { text: modelData.name; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                            }
                            MouseArea {
                                id: mMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.copyText(modelData.codename)
                            }
                        }
                        EmptyState {
                            anchors.fill: parent
                            visible: page.models.length === 0
                            icon: "search"
                            title: "没有匹配的机型"
                            text: "换一个代号或系列名再试。"
                        }
                    }

                    Column {
                        width: parent.width
                        height: 110
                        spacing: 3
                        Text {
                            text: "官方固件 / 下载入口"
                            color: Theme.textMuted
                            font.pixelSize: 10
                        }
                        Flow {
                            width: parent.width
                            height: childrenRect.height
                            spacing: 4
                            Repeater {
                                model: (page.brandInfo.official || [])
                                delegate: AppButton {
                                    text: modelData.title
                                    icon: "external-link"
                                    kind: 1
                                    small: true
                                    onClicked: App.openUrl(modelData.url)
                                }
                            }
                            Text {
                                visible: (page.brandInfo.official || []).length === 0
                                text: (page.brandInfo.searchHint || "该品牌暂无内置在线入口，可使用上方「在线检索」工具。")
                                color: Theme.textMuted
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }
                        }
                        Text {
                            text: (page.brandInfo.notes || []).join("  ·  ")
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            width: parent.width
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    ToolSheet { id: searchSheet; anchors.fill: parent }
}
