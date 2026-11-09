import QtQuick
import QtQuick.Controls.Basic
import XMF

Item {
    id: page
    property string categoryFilter: ""

    property string category: categoryFilter
    property string search: ""
    property bool onlyRunnable: false

    readonly property var filtered: {
        const out = []
        const all = App.tools
        const q = search.trim().toLowerCase()
        for (let i = 0; i < all.length; ++i) {
            const t = all[i]
            if (category !== "" && t.category !== category)
                continue
            if (onlyRunnable && t.runnable === false)
                continue
            if (q !== "") {
                const hay = (t.name + " " + t.desc + " " + t.keywords + " " + t.id).toLowerCase()
                if (hay.indexOf(q) < 0)
                    continue
            }
            out.push(t)
        }
        return out
    }

    function categoryName(id) {
        if (id === "")
            return "全部"
        const cats = App.categories
        for (let i = 0; i < cats.length; ++i)
            if (cats[i].id === id)
                return cats[i].name
        return id
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 6

        PageHeader {
            width: parent.width
            title: category === "" ? "工具库" : categoryName(category)
            subtitle: filtered.length + " / " + App.tools.length + " 个工具"
            icon: "disc-3"
            SearchField {
                width: 200
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

        // category tabs
        Flickable {
            width: parent.width
            height: 26
            contentWidth: tabs.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: tabs
                spacing: 0
                Repeater {
                    model: [{ id: "", name: "全部" }].concat(App.categories)
                    delegate: Item {
                        width: tabText.implicitWidth + 22
                        height: 26
                        Rectangle {
                            anchors.fill: parent
                            color: page.category === modelData.id ? Theme.selection
                                                                  : (tabMouse.containsMouse ? Theme.panelHover : "transparent")
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: Theme.accent
                            visible: page.category === modelData.id
                        }
                        Text {
                            id: tabText
                            anchors.centerIn: parent
                            text: modelData.name
                            color: page.category === modelData.id ? Theme.accent : Theme.textSub
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.category = modelData.id
                        }
                    }
                }
            }
        }

        // table header
        Rectangle {
            width: parent.width
            height: 24
            color: Theme.panelAlt
            border.width: 1
            border.color: Theme.border
            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8
                Text { width: 22; text: "" }
                Text { width: Math.min(220, parent.width * 0.22); text: "工具"; color: Theme.textMuted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                Text { width: parent.width - Math.min(220, parent.width * 0.22) - 250; text: "说明"; color: Theme.textMuted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                Text { width: 56; text: "状态"; color: Theme.textMuted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                Item { width: 30; height: 1 }
                Text { text: "操作"; color: Theme.textMuted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            }
        }

        Item {
            width: parent.width
            height: parent.height - 128

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: page.filtered
                delegate: ToolCard {
                    toolData: modelData
                    onActivate: sheet.open(modelData)
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
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

    ToolSheet { id: sheet; anchors.fill: parent }
}
