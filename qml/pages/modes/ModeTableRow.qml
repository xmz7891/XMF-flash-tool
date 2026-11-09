import QtQuick
import QtQuick.Layouts
import XMF

// 表格行：悬停高亮 + 分隔线 + 可选整行点选。
// 内容（单元格）默认加进内部 RowLayout，页面按列给 Layout.* 即可对齐。
// 勾选状态由外部模型持有：组件只发 toggled，不回写 checked。
Item {
    id: root

    property bool checkable: false
    property bool checked: true
    property bool hovered: false
    property bool showDivider: true
    property int rowHeight: 32
    property color accentColor: Theme.accent

    default property alias content: layout.data
    signal toggled(bool value)

    implicitHeight: rowHeight
    height: rowHeight

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.r6
        color: root.hovered ? Theme.glassHover : "transparent"

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
        visible: root.showDivider && !root.hovered
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.checkable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onEntered: root.hovered = true
        onExited: root.hovered = false
        onClicked: if (root.checkable) root.toggled(!root.checked)
    }
}
