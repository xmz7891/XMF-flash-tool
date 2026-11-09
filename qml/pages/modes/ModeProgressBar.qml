import QtQuick
import XMF

// 细进度条：数值进度带过渡动画，indeterminate 时显示循环移动的色块。
Item {
    id: root

    property real value: 0
    property bool indeterminate: false
    property color color: Theme.accent
    property real barHeight: 5
    property real animValue: value

    implicitWidth: 200
    implicitHeight: barHeight
    height: barHeight

    Behavior on animValue { NumberAnimation { duration: Theme.durSlow; easing.type: Theme.easing } }
    onValueChanged: animValue = value

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: Theme.glassStrong
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.indeterminate ? 0 : Math.max(0, Math.min(1, root.animValue / 100)) * parent.width
        radius: height / 2
        color: root.color
        visible: !root.indeterminate

        Behavior on width { NumberAnimation { duration: Theme.durSlow; easing.type: Theme.easing } }
    }

    Rectangle {
        id: shimmer
        visible: root.indeterminate
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(48, parent.width * 0.22)
        radius: height / 2
        color: root.color
        opacity: 0.55

        SequentialAnimation on x {
            running: Theme.animations && root.indeterminate
            loops: Animation.Infinite
            NumberAnimation {
                from: -shimmer.width
                to: root.width
                duration: 1500
                easing.type: Easing.InOutSine
            }
        }
    }
}
