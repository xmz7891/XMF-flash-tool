import QtQuick
import QtQuick.Layouts
import XMF

// 玻璃卡片：标题行 + 可选说明 + 内容插槽，可选右上角操作插槽。
// 内容默认放在 body（fill 剩余空间），actions 放在标题行右侧。
GlassSurface {
    id: root

    property string title: ""
    property string subtitle: ""
    property string icon: ""
    property string hint: ""

    default property alias content: body.data
    property alias actions: actionsRow.data

    radius: Theme.r18
    color: Theme.glass

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            spacing: 8

            IconImage {
                visible: root.icon !== ""
                Layout.alignment: Qt.AlignVCenter
                name: root.icon
                size: 14
                tint: root.accentColor
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.title
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: root.subtitle !== ""
                text: "· " + root.subtitle
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
                Layout.maximumWidth: 300
            }

            Item { Layout.fillWidth: true }

            Row {
                id: actionsRow
                spacing: 8
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            visible: root.hint !== ""
            Layout.preferredHeight: visible ? implicitHeight : 0
            text: root.hint
            color: Theme.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            lineHeight: 1.35
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
        }
    }
}
