import QtQuick
import QtQuick.Layouts
import XMF

// 状态灯：小圆点 + 标签 + 右侧状态文字。
// stateName: idle / ready / busy / ok / fail，只影响圆点与右侧状态文字的颜色。
Item {
    id: root

    property string stateName: "idle"
    property string label: ""
    property string detail: ""
    property color lampColor: {
        if (root.stateName === "ready")
            return Theme.accent
        if (root.stateName === "busy")
            return Theme.warning
        if (root.stateName === "ok")
            return Theme.success
        if (root.stateName === "fail")
            return Theme.danger
        return Theme.textMuted
    }
    property int dotSize: 7

    implicitWidth: 140
    implicitHeight: 20
    height: 20

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.preferredWidth: root.dotSize
            Layout.preferredHeight: root.dotSize
            Layout.alignment: Qt.AlignVCenter
            radius: root.dotSize / 2
            color: root.lampColor
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: Theme.textSub
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.detail !== ""
            text: root.detail
            color: root.stateName === "idle" ? Theme.textMuted : root.lampColor
            font.pixelSize: 10
        }
    }
}
