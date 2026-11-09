import QtQuick
import XMF

Item {
    id: root
    property string icon: "package"
    property string title: "暂无内容"
    property string text: ""
    property string actionText: ""
    signal action()

    implicitHeight: 160

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 420)
        spacing: 6

        IconImage {
            anchors.horizontalCenter: parent.horizontalCenter
            name: root.icon
            size: 22
            tint: Theme.textMuted
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.title
            color: Theme.textSub
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Text {
            visible: root.text !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.text
            color: Theme.textMuted
            font.pixelSize: 11
            lineHeight: 1.4
        }
        AppButton {
            visible: root.actionText !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.actionText
            kind: 1
            small: true
            onClicked: root.action()
        }
    }
}
