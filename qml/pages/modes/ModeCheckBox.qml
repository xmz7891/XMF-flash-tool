import QtQuick
import XMF

// 玻璃勾选框：16px 圆角方块，选中时用 accent 填充并显示对勾。
// 组件自身不写回 checked（保持与模型的双向约束），只发出 toggled 信号。
Item {
    id: root

    property bool checked: false
    property color accentColor: Theme.accent
    property int boxSize: 16
    signal toggled(bool value)

    implicitWidth: boxSize
    implicitHeight: boxSize
    width: boxSize
    height: boxSize

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: root.checked ? root.accentColor
                            : (mouse.containsMouse ? Theme.glassStrong : "transparent")
        border.width: 1
        border.color: root.checked ? "transparent"
                                   : (mouse.containsMouse ? Theme.borderStrong : Theme.border)

        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

        IconImage {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"
            size: 11
            tint: Theme.textOnAccent
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
