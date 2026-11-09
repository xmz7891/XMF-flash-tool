import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import XMF

// 内置刷机模式工作台的可复用外框。
//
// 结构（从上到下）：
//   标题栏  —— 返回按钮 + 模式图标 / 标题 / 标签 + headerActions 插槽
//   固件栏  —— 固件路径输入 + 浏览 / 加载 + firmwareActions 插槽
//   内容区  —— default 属性，页面自行组织（表格 / 选项 / 日志…）
//   状态栏  —— 状态灯 + 状态文字 + 进度条 + 百分比 + 设备状态
//
// 页面只负责填充属性与内容；「返回」通过 backRequested() 上行，
// 不直接依赖 Main.qml / ModesPage.qml。
Item {
    id: root

    // ---- 标题 ------------------------------------------------------------
    property string title: ""
    property string subtitle: ""
    property string icon: "layout-dashboard"
    property string tag: ""
    property color accentColor: Theme.accent

    // ---- 固件栏 ----------------------------------------------------------
    property string firmwareLabel: "固件"
    property string firmwarePlaceholder: "选择固件…"
    property string fwPath: ""
    property bool fwLoaded: false

    // ---- 底部状态 --------------------------------------------------------
    property real progress: 0
    property bool indeterminate: false
    property string statusText: "就绪"
    property string statusKind: "idle" // idle / ready / busy / ok / fail
    property string deviceText: "未检测到设备"
    property string deviceKind: "idle"
    property string speedText: "—"
    property string deviceCode: "—"
    property string bootloaderText: "未知"
    property string slotText: "—"

    // ---- 插槽 ------------------------------------------------------------
    default property alias content: contentArea.data
    property alias headerActions: headerActionsRow.data
    property alias firmwareActions: firmwareActionsRow.data

    signal backRequested()
    signal browseRequested()
    signal loadRequested()
    signal reloadRequested()

    implicitWidth: 900
    implicitHeight: 640

    function kindColor(kind) {
        if (kind === "ready")
            return root.accentColor
        if (kind === "busy")
            return Theme.warning
        if (kind === "ok")
            return Theme.success
        if (kind === "fail")
            return Theme.danger
        return Theme.textMuted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // ------------------------------------------------------- 标题栏
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 52

            RowLayout {
                anchors.fill: parent
                spacing: 12

                AppButton {
                    text: "返回"
                    icon: "arrow-left"
                    kind: 2
                    small: true
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.backRequested()
                }

                Rectangle {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.r12
                    color: Theme.glassStrong
                    border.width: 1
                    border.color: Theme.border

                    IconImage {
                        anchors.centerIn: parent
                        name: root.icon
                        size: 19
                        tint: Theme.text
                    }
                }

                Column {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2

                    Row {
                        spacing: 8
                        Text {
                            text: root.title
                            color: Theme.text
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            visible: root.tag !== ""
                            width: tagLabel.implicitWidth + 12
                            height: 16
                            radius: 8
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.glassStrong
                            border.width: 1
                            border.color: Theme.border

                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: root.tag
                                color: Theme.textMuted
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: root.subtitle
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                Row {
                    id: headerActionsRow
                    spacing: 8
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        // ------------------------------------------------------- 固件栏
        GlassSurface {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: Theme.r18
            color: Theme.glass

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.r10
                    color: Theme.glassStrong

                    IconImage {
                        anchors.centerIn: parent
                        name: "file-archive"
                        size: 15
                        tint: Theme.textSub
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: root.firmwareLabel
                    color: Theme.textMuted
                    font.pixelSize: 11
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    Layout.alignment: Qt.AlignVCenter
                    radius: 10
                    color: fwField.activeFocus ? Theme.glassHover : Theme.glass
                    border.width: 1
                    border.color: fwField.activeFocus ? Theme.borderAccent : Theme.border

                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

                    TextField {
                        id: fwField
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        color: Theme.text
                        font.pixelSize: 12
                        selectByMouse: true
                        background: null
                        padding: 0
                        placeholderText: root.firmwarePlaceholder
                        placeholderTextColor: Theme.textMuted
                        text: root.fwPath
                        onTextEdited: root.fwPath = text
                    }
                }

                Rectangle {
                    visible: root.fwLoaded
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 20
                    radius: Theme.r10
                    color: Theme.glassStrong
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "已加载"
                        color: Theme.success
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                }

                AppButton {
                    text: "浏览"
                    icon: "folder-open"
                    kind: 1
                    small: true
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.browseRequested()
                }

                AppButton {
                    text: root.fwLoaded ? "重新加载" : "加载"
                    icon: root.fwLoaded ? "refresh-cw" : "play"
                    kind: 0
                    small: true
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.loadRequested()
                }

                Row {
                    id: firmwareActionsRow
                    spacing: 8
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        // ------------------------------------------------------- 内容区
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
        }

        // ------------------------------------------------------- 状态栏
        GlassSurface {
            Layout.fillWidth: true
            Layout.preferredHeight: 76
            radius: Theme.r18
            color: Theme.glass

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ModeStatusLamp {
                        Layout.preferredWidth: 200
                        Layout.alignment: Qt.AlignVCenter
                        stateName: root.statusKind
                        label: root.statusText
                        lampColor: root.kindColor(root.statusKind)
                    }

                    ModeProgressBar {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        value: root.progress
                        indeterminate: root.indeterminate
                        color: root.accentColor
                    }

                    Text {
                        Layout.preferredWidth: 40
                        Layout.alignment: Qt.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        text: root.indeterminate ? "…" : Math.round(root.progress) + "%"
                        color: Theme.textSub
                        font.pixelSize: 11
                        font.family: "Consolas"
                    }

                    Text {
                        Layout.preferredWidth: 84
                        Layout.alignment: Qt.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        text: root.speedText
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.family: "Consolas"
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 16
                        Layout.alignment: Qt.AlignVCenter
                        color: Theme.divider
                    }

                    ModeStatusLamp {
                        Layout.preferredWidth: 150
                        Layout.alignment: Qt.AlignVCenter
                        stateName: root.deviceKind
                        label: root.deviceText
                    }

                    Rectangle {
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        Layout.alignment: Qt.AlignVCenter
                        radius: 8
                        color: reloadMouse.containsMouse ? Theme.glassHover : "transparent"

                        Behavior on color { ColorAnimation { duration: Theme.durFast } }

                        IconImage {
                            anchors.centerIn: parent
                            name: "refresh-cw"
                            size: 12
                            tint: reloadMouse.containsMouse ? Theme.text : Theme.textMuted
                        }

                        MouseArea {
                            id: reloadMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.reloadRequested()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    spacing: 16

                    Text {
                        Layout.fillWidth: true
                        text: "设备代号 " + root.deviceCode + "     BL 状态 " + root.bootloaderText
                              + "     当前槽位 " + root.slotText
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    Text {
                        text: "工作台 · 刷写后端已接入（Fastboot）"
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    // 外部（例如文件对话框）改写 fwPath 时同步输入框
    Connections {
        target: root
        function onFwPathChanged() {
            if (fwField.text !== root.fwPath)
                fwField.text = root.fwPath
        }
    }
}
