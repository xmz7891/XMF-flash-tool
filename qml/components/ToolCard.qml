import QtQuick
import XMF

// Dense table row for the tool library.
Item {
    id: root
    property var toolData: ({})
    signal activate()

    implicitHeight: 30
    width: ListView.view ? ListView.view.width : parent.width

    Rectangle {
        anchors.fill: parent
        color: mouse.containsMouse ? Theme.panelHover : (index % 2 === 1 ? Theme.panelAlt : "transparent")
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        IconImage {
            width: 16
            name: root.toolData.icon || "tool"
            size: 14
            tint: mouse.containsMouse ? Theme.accent : Theme.textSub
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            width: Math.min(220, parent.width * 0.22)
            text: root.toolData.name || ""
            color: Theme.text
            font.pixelSize: 12
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            width: parent.width - Math.min(220, parent.width * 0.22) - 250
            text: root.toolData.desc || ""
            color: Theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            width: 56
            text: (root.toolData.maturity || 0) === 2 ? "开发中"
                  : ((root.toolData.maturity || 0) === 1 ? "Beta" : "")
            color: Theme.textMuted
            font.pixelSize: 10
            anchors.verticalCenter: parent.verticalCenter
        }

        Item { width: 30; height: 1 }

        Text {
            text: root.toolData.runnable === false ? "" : "打开"
            color: mouse.containsMouse ? Theme.accent : Theme.textMuted
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activate()
    }
}
