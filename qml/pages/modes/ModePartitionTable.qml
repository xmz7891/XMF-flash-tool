import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import XMF

// 分区 / 镜像表：数据来自 App.workspace 的解析结果（实时数据，不做演示占位）。
// 未加载时显示 EmptyState；加载后按 columns 描述渲染列，支持行悬停与勾选。
//
// columns: [{ label, role, width, flex, align, mono }]
//   role 取 name / size / address / file / detail / checkState / stateText
Item {
    id: root

    property var columns: []
    property bool checkable: false
    property int count: 0
    property int selectedCount: 0
    property bool allSelected: false

    property string emptyIcon: "file-archive"
    property string emptyTitle: "尚未加载固件"
    property string emptyText: ""
    property string emptyActionText: "浏览选择"
    property string rowActionText: "刷写"

    // 内部数据（勾选状态保存在这里，刷新时整体重建）
    property var rows: []
    property int revision: 0

    signal emptyAction()
    signal selectionChanged()
    signal rowAction(int index)

    function setPartitions(list) {
        const data = []
        const source = list || []
        for (let i = 0; i < source.length; ++i) {
            const p = source[i] || {}
            data.push({
                name: p.name !== undefined && p.name !== null ? String(p.name) : "",
                size: p.sizeText !== undefined && p.sizeText !== null ? String(p.sizeText) : "",
                address: p.address !== undefined && p.address !== null ? String(p.address) : "",
                file: p.file !== undefined && p.file !== null ? String(p.file) : "",
                detail: p.detail !== undefined && p.detail !== null ? String(p.detail) : "",
                region: p.region !== undefined && p.region !== null ? String(p.region) : "",
                checked: p.checked !== false
            })
        }
        root.rows = data
        root.count = data.length
        root.revision += 1
        root.updateSelection()
    }

    function clearAll() {
        root.rows = []
        root.count = 0
        root.revision += 1
        root.updateSelection()
    }

    function rowAt(i) {
        return i >= 0 && i < root.rows.length ? root.rows[i] : null
    }

    function textAt(i, role) {
        const r = rowAt(i)
        if (!r)
            return ""
        if (role === "checkState")
            return r.checked ? "已选择" : "跳过"
        if (role === "stateText")
            return r.file.length > 0 ? "已加载" : "—"
        if (role === "lun")
            return r.region.length > 0 ? r.region : "—"
        let v = r[role]
        if ((v === undefined || v === null || v === "") && role === "file")
            v = r.region
        if (v === undefined || v === null || v === "")
            return "—"
        return String(v)
    }

    function cellColor(i, role) {
        const r = rowAt(i)
        if (role === "checkState")
            return r && r.checked ? Theme.accent : Theme.textMuted
        if (role === "stateText")
            return r && r.file.length > 0 ? Theme.success : Theme.textMuted
        if (role === "name")
            return Theme.text
        if (role === "size" || role === "address" || role === "lun")
            return Theme.textSub
        return Theme.textMuted
    }

    function fixedWidth() {
        let total = 0
        for (let i = 0; i < root.columns.length; ++i) {
            const c = root.columns[i]
            if (c.flex !== true)
                total += c.width || 80
        }
        return total
    }

    function cellWidth(totalWidth, col) {
        const n = root.columns.length
        const check = root.checkable ? 16 + 8 : 0
        const gaps = 8 * Math.max(0, n - 1 + (root.checkable ? 1 : 0))
        if (col.flex === true)
            return Math.max(60, totalWidth - check - fixedWidth() - gaps)
        return col.width || 80
    }

    function toggleRow(i) {
        const r = rowAt(i)
        if (!r)
            return
        r.checked = !r.checked
        root.revision += 1
        root.updateSelection()
    }

    function setAll(value) {
        for (let i = 0; i < root.rows.length; ++i)
            root.rows[i].checked = value
        root.revision += 1
        root.updateSelection()
    }

    function updateSelection() {
        let n = 0
        for (let i = 0; i < root.rows.length; ++i) {
            if (root.rows[i].checked)
                n += 1
        }
        root.selectedCount = n
        root.allSelected = root.count > 0 && n === root.count
        root.selectionChanged()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 表头
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.count > 0 ? 26 : 0
            visible: root.count > 0

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Item {
                    visible: root.checkable
                    width: root.checkable ? 16 : 0
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter
                }

                Repeater {
                    model: root.columns

                    delegate: Text {
                        width: root.cellWidth(parent.width, modelData)
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: modelData.align === "right" ? Text.AlignRight : Text.AlignLeft
                        text: modelData.label || ""
                        color: Theme.textSub
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
            visible: root.count > 0
        }

        // ---- 表格行
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.count > 0
            clip: true
            contentHeight: listColumn.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: listColumn
                width: flick.width

                Repeater {
                    model: root.count

                    delegate: Item {
                        id: rowItem
                        width: listColumn.width
                        height: 32

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.r6
                            color: rowMouse.containsMouse ? Theme.glassHover : "transparent"

                            Behavior on color { ColorAnimation { duration: Theme.durFast } }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.divider
                            visible: !rowMouse.containsMouse
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8

                            Item {
                                visible: root.checkable
                                width: root.checkable ? 16 : 0
                                height: 16
                                anchors.verticalCenter: parent.verticalCenter

                                ModeCheckBox {
                                    anchors.centerIn: parent
                                    checked: {
                                        root.revision
                                        const r = root.rowAt(rowItem.rowIndex)
                                        return r ? r.checked : false
                                    }
                                    onToggled: function (value) { root.toggleRow(rowItem.rowIndex) }
                                }
                            }

                            Repeater {
                                model: root.columns

                                delegate: Item {
                                    width: root.cellWidth(parent.width, modelData)
                                    height: 20
                                    anchors.verticalCenter: parent.verticalCenter
                                    property bool isAction: modelData.role === "action"

                                    Text {
                                        visible: !parent.isAction
                                        anchors.fill: parent
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: modelData.align === "right" ? Text.AlignRight : Text.AlignLeft
                                        text: {
                                            root.revision
                                            return root.textAt(rowItem.rowIndex, modelData.role)
                                        }
                                        color: {
                                            root.revision
                                            return root.cellColor(rowItem.rowIndex, modelData.role)
                                        }
                                        font.pixelSize: modelData.role === "name" ? 11 : 10
                                        font.weight: modelData.role === "name" ? Font.Medium : Font.Normal
                                        font.family: modelData.mono === true ? "Consolas" : Theme.fontFamily
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        visible: parent.isAction
                                        anchors.centerIn: parent
                                        width: actionLabel.implicitWidth + 14
                                        height: 20
                                        radius: Theme.r6
                                        color: actionMouse.containsMouse ? Theme.glassStrong : "transparent"
                                        border.width: 1
                                        border.color: actionMouse.containsMouse ? Theme.borderStrong : Theme.border

                                        Behavior on color { ColorAnimation { duration: Theme.durFast } }

                                        Text {
                                            id: actionLabel
                                            anchors.centerIn: parent
                                            text: root.rowActionText
                                            color: Theme.textSub
                                            font.pixelSize: 9
                                        }

                                        MouseArea {
                                            id: actionMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.rowAction(rowItem.rowIndex)
                                        }
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            cursorShape: root.checkable ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (root.checkable) root.toggleRow(rowItem.rowIndex)
                        }

                        property int rowIndex: index
                    }
                }
            }
        }

        // ---- 空状态
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.count === 0
            icon: root.emptyIcon
            title: root.emptyTitle
            text: root.emptyText
            actionText: root.emptyActionText
            onAction: root.emptyAction()
        }
    }
}
