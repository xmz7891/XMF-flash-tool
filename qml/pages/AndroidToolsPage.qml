import QtQuick
import QtQuick.Controls.Basic
import XMF

// Android tinkering shelf: card-first layout instead of a dense table.
// Root / ADB / Fastboot / apps / scrcpy / TWRP / backup tools are grouped,
// searchable and opened through the shared ToolSheet.
Item {
    id: page

    property string group: "all"
    property string search: ""

    readonly property var groups: [
        { id: "all", name: "全部", icon: "grid-2x2", color: "#E8A33D",
          desc: "所有安卓工具" },
        { id: "root", name: "Root / 模块", icon: "key-round", color: "#34D399",
          desc: "Magisk / KSU / APatch" },
        { id: "adb", name: "ADB", icon: "terminal", color: "#38BDF8",
          desc: "Shell / 文件 / 无线" },
        { id: "fastboot", name: "Fastboot", icon: "zap", color: "#FB7185",
          desc: "分区 / 解锁 / 槽位" },
        { id: "apps", name: "应用管理", icon: "package", color: "#A78BFA",
          desc: "冻结 / 提取 / 安装" },
        { id: "scrcpy", name: "投屏", icon: "monitor-smartphone", color: "#22D3EE",
          desc: "参数化 scrcpy" },
        { id: "twrp", name: "TWRP", icon: "table", color: "#F2A93B",
          desc: "分区读 / 写 / 擦" },
        { id: "backup", name: "备份", icon: "save", color: "#84CC16",
          desc: "媒体 / 通讯录 / Boot" }
    ]

    function idOf(t) { return String(t.id || "") }
    function isAndroid(t) {
        const c = String(t.category || "")
        const id = idOf(t)
        return c === "root" || c === "adb" || c === "bootloader" || c === "unlock"
                || c === "bypass" || id.indexOf("twrp.") === 0
                || id.indexOf("scrcpy.") === 0
    }
    function matches(t, g) {
        if (g === "all") return isAndroid(t)
        const id = idOf(t)
        const c = String(t.category || "")
        if (g === "root") return c === "root" || id.indexOf("root.") === 0 || id.indexOf("apatch.") === 0
        if (g === "adb") return c === "adb" && id.indexOf("twrp.") !== 0
                && id.indexOf("scrcpy.") !== 0 && id.indexOf("adb.app.") !== 0
                && id.indexOf("adb.apk.") !== 0 && id.indexOf("adb.install") !== 0
                && id.indexOf("adb.media.") !== 0 && id.indexOf("adb.contacts.") !== 0
        if (g === "fastboot") return c === "bootloader" || id.indexOf("fastboot.") === 0
        if (g === "apps") return id.indexOf("adb.app.") === 0 || id.indexOf("adb.apk.") === 0
                || id.indexOf("adb.install") === 0
        if (g === "scrcpy") return id.indexOf("scrcpy") === 0
        if (g === "twrp") return id.indexOf("twrp.") === 0
        if (g === "backup") return id.indexOf("adb.media.") === 0
                || id.indexOf("adb.contacts.") === 0 || id.indexOf("root.boot.") === 0
        return false
    }
    function countOf(g) {
        let n = 0
        for (let i = 0; i < App.tools.length; ++i)
            if (matches(App.tools[i], g)) ++n
        return n
    }

    readonly property var filtered: {
        const out = []
        const q = search.trim().toLowerCase()
        const all = App.tools
        for (let i = 0; i < all.length; ++i) {
            const t = all[i]
            if (!matches(t, group)) continue
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
        spacing: 10

        PageHeader {
            width: parent.width
            title: "安卓工具"
            subtitle: "Root · ADB · Fastboot · 应用 · 投屏 · TWRP · 备份"
            icon: "smartphone"

            SearchField {
                width: 220
                placeholder: "搜索工具 / 模块 / 关键词"
                onTextChanged: page.search = text
            }
            Rectangle {
                width: 92; height: 28; radius: 8
                color: Theme.alpha(Theme.accent, 0.12)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.35)
                Text {
                    anchors.centerIn: parent
                    text: page.filtered.length + " 个工具"
                    color: Theme.accent
                    font.pixelSize: Theme.fs(10)
                }
            }
        }

        // category cards
        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: page.groups
                delegate: Rectangle {
                    width: Math.max(150, (parent.width - 7 * 8) / 8)
                    height: 82
                    radius: Theme.r14
                    color: page.group === modelData.id ? Theme.alpha(modelData.color, 0.15)
                                                       : (catMouse.containsMouse ? Theme.panelHover : Theme.panel)
                    border.width: page.group === modelData.id ? 2 : 1
                    border.color: page.group === modelData.id ? modelData.color
                                                              : (catMouse.containsMouse ? Theme.borderStrong : Theme.border)

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        spacing: 8
                        Rectangle {
                            width: 28; height: 28; radius: 8
                            color: Theme.alpha(modelData.color, 0.16)
                            IconImage {
                                anchors.centerIn: parent
                                name: modelData.icon
                                size: 15
                                tint: modelData.color
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fs(11)
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.top: parent.top
                        anchors.topMargin: 48
                        width: parent.width - 24
                        text: modelData.desc
                        color: Theme.textMuted
                        font.pixelSize: Theme.fs(9)
                        elide: Text.ElideRight
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 10
                        text: page.countOf(modelData.id)
                        color: modelData.color
                        font.pixelSize: Theme.fs(10)
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: catMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.group = modelData.id
                    }
                }
            }
        }

        // tool cards (grid, never a table)
        GridView {
            id: grid
            width: parent.width
            height: Math.max(120, parent.height - 170)
            clip: true
            cellWidth: Math.max(250, Math.floor(width / 3))
            cellHeight: 104
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
                    border.color: toolMouse.containsMouse ? Theme.alpha(Theme.accent, 0.55) : Theme.border

                    Row {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Rectangle {
                            width: 38; height: 38; radius: 10
                            color: Theme.alpha(Theme.accent, 0.10)
                            IconImage {
                                anchors.centerIn: parent
                                name: modelData.icon || "tool"
                                size: 18
                                tint: Theme.accent
                            }
                        }
                        Column {
                            width: parent.width - 52
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
                                    color: Theme.warning
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
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "›"
                            color: toolMouse.containsMouse ? Theme.accent : Theme.textDim
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

        Text {
            width: parent.width
            visible: page.filtered.length === 0
            text: "没有匹配的安卓工具，换个关键词试试。"
            color: Theme.textMuted
            font.pixelSize: Theme.fs(11)
            horizontalAlignment: Text.AlignHCenter
        }
    }

    ToolSheet { id: sheet; anchors.fill: parent }
}
