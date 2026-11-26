import QtQuick
import QtQuick.Controls.Basic
import XMF

// Tool library: category rail on the left, card grid on the right. This
// replaces the old dense table so the 250+ tools stay scannable.
Item {
    id: page

    property string categoryFilter: ""
    property string category: categoryFilter
    property string search: ""
    property bool onlyRunnable: false

    readonly property var categoryList: [{ id: "", name: "全部", icon: "grid-2x2", color: Theme.accent }].concat(App.categories)

    function countFor(id) {
        let n = 0
        for (let i = 0; i < App.tools.length; ++i) {
            const t = App.tools[i]
            if (id !== "" && t.category !== id) continue
            if (onlyRunnable && t.runnable === false) continue
            ++n
        }
        return n
    }

    function categoryName(id) {
        for (let i = 0; i < categoryList.length; ++i)
            if (categoryList[i].id === id) return categoryList[i].name
        return id
    }
    function categoryColor(id) {
        for (let i = 0; i < categoryList.length; ++i)
            if (categoryList[i].id === id) return categoryList[i].color
        return Theme.accent
    }

    readonly property var filtered: {
        const out = []
        const q = search.trim().toLowerCase()
        const all = App.tools
        for (let i = 0; i < all.length; ++i) {
            const t = all[i]
            if (category !== "" && t.category !== category) continue
            if (onlyRunnable && t.runnable === false) continue
            if (q !== "") {
                const hay = (t.name + " " + t.desc + " " + t.keywords + " " + t.id).toLowerCase()
                if (hay.indexOf(q) < 0) continue
            }
            out.push(t)
        }
        return out
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 8

        PageHeader {
            width: parent.width
            title: category === "" ? "工具库" : categoryName(category)
            subtitle: filtered.length + " 个工具 · 共 " + App.tools.length + " 个"
            icon: "disc-3"

            SearchField {
                width: 230
                placeholder: "搜索名称、说明、格式"
                onTextChanged: page.search = text
            }
            AppButton {
                text: page.onlyRunnable ? "仅可运行 ✓" : "仅可运行"
                kind: page.onlyRunnable ? 0 : 1
                small: true
                onClicked: page.onlyRunnable = !page.onlyRunnable
            }
        }

        Row {
            width: parent.width
            height: parent.height - 60
            spacing: 10

            // category rail
            GlassSurface {
                width: 216
                height: parent.height
                title: "分类"

                ListView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 4
                    model: page.categoryList
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 42
                        radius: Theme.r10
                        color: page.category === modelData.id ? Theme.alpha(page.categoryColor(modelData.id), 0.14)
                                                              : (railMouse.containsMouse ? Theme.panelHover : "transparent")
                        border.width: page.category === modelData.id ? 1 : 0
                        border.color: Theme.alpha(page.categoryColor(modelData.id), 0.45)

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Rectangle {
                                width: 24; height: 24; radius: 7
                                color: Theme.alpha(page.categoryColor(modelData.id), 0.15)
                                IconImage {
                                    anchors.centerIn: parent
                                    name: modelData.icon || "folder"
                                    size: 13
                                    tint: page.categoryColor(modelData.id)
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 104
                                text: modelData.name
                                color: page.category === modelData.id ? Theme.text : Theme.textSub
                                font.pixelSize: Theme.fs(10)
                                elide: Text.ElideRight
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.countFor(modelData.id)
                                color: page.category === modelData.id ? page.categoryColor(modelData.id) : Theme.textDim
                                font.pixelSize: Theme.fs(9)
                            }
                        }
                        MouseArea {
                            id: railMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.category = modelData.id
                        }
                    }
                }
            }

            // tool card grid
            Item {
                width: parent.width - 226
                height: parent.height

                GridView {
                    id: grid
                    anchors.fill: parent
                    clip: true
                    cellWidth: Math.max(250, Math.floor(width / 3))
                    cellHeight: 112
                    model: page.filtered
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        width: grid.cellWidth - 8
                        height: grid.cellHeight - 8

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.r14
                            color: toolMouse.containsMouse ? Theme.panelHover : Theme.panel
                            border.width: 1
                            border.color: toolMouse.containsMouse
                                          ? Theme.alpha(page.categoryColor(modelData.category), 0.65)
                                          : Theme.border

                            Rectangle {
                                width: 3
                                height: parent.height - 24
                                radius: 2
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                color: page.categoryColor(modelData.category)
                                opacity: 0.75
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 12
                                anchors.topMargin: 12
                                anchors.bottomMargin: 12
                                spacing: 10

                                Rectangle {
                                    width: 38; height: 38; radius: 10
                                    color: Theme.alpha(page.categoryColor(modelData.category), 0.12)
                                    IconImage {
                                        anchors.centerIn: parent
                                        name: modelData.icon || "tool"
                                        size: 18
                                        tint: page.categoryColor(modelData.category)
                                    }
                                }

                                Column {
                                    width: parent.width - 90
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4

                                    Row {
                                        width: parent.width
                                        spacing: 6
                                        Text {
                                            width: parent.width - 60
                                            text: modelData.name || modelData.id
                                            color: Theme.text
                                            font.pixelSize: Theme.fs(12)
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: (modelData.maturity || 0) > 0
                                            text: (modelData.maturity || 0) === 2 ? "开发中" : "Beta"
                                            color: (modelData.maturity || 0) === 2 ? Theme.textMuted : Theme.warning
                                            font.pixelSize: Theme.fs(8)
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.desc || ""
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fs(9)
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: (modelData.runnable === false ? "未接入" : "可运行")
                                              + " · " + page.categoryName(modelData.category)
                                        color: modelData.runnable === false ? Theme.textDim : Theme.textSub
                                        font.pixelSize: Theme.fs(8)
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "›"
                                    color: toolMouse.containsMouse ? page.categoryColor(modelData.category) : Theme.textDim
                                    font.pixelSize: Theme.fs(20)
                                }
                            }
                        }

                        MouseArea {
                            id: toolMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sheet.open(modelData)
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: page.filtered.length === 0
                    icon: "search"
                    title: "没有匹配的工具"
                    text: "调整分类或关键词；也可以在首页查看常用流程。"
                }
            }
        }
    }

    ToolSheet { id: sheet; anchors.fill: parent }
}
