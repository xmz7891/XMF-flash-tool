import QtQuick
import XMF

// Compact status tile (kept for pages that still use the old dashboard widget).
Item {
    id: root
    property string label: ""
    property var value: ""
    property string unit: ""
    property string icon: ""
    property color accentColor: Theme.accent
    property string hint: ""
    implicitHeight: 52

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        Text {
            text: root.label
            color: Theme.textMuted
            font.pixelSize: 10
        }
        Row {
            spacing: 3
            Text {
                text: root.value
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.unit !== ""
                text: root.unit
                color: Theme.textMuted
                font.pixelSize: 10
                anchors.baseline: parent.bottom
                anchors.baselineOffset: -2
            }
        }
    }
}
