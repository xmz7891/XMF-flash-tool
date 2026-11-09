import QtQuick
import XMF

// Thin progress ring for the task dock.
Item {
    id: root
    property real value: 0
    property real thickness: 3
    property color trackColor: Theme.divider
    property color color: Theme.accent
    property real animValue: value

    implicitWidth: 22
    implicitHeight: 22

    Behavior on animValue { NumberAnimation { duration: Theme.durSlow; easing.type: Theme.easing } }
    onValueChanged: animValue = value

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const cx = width / 2, cy = height / 2;
            const r = Math.min(width, height) / 2 - root.thickness / 2 - 1;
            const start = -Math.PI / 2;
            ctx.lineWidth = root.thickness;
            ctx.lineCap = "round";
            ctx.strokeStyle = root.trackColor;
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.stroke();
            const p = Math.max(0.02, Math.min(1, root.animValue / 100));
            ctx.strokeStyle = root.color;
            ctx.beginPath();
            ctx.arc(cx, cy, r, start, start + Math.PI * 2 * p);
            ctx.stroke();
        }
        Connections {
            target: root
            function onAnimValueChanged() { canvas.requestPaint(); }
            function onColorChanged() { canvas.requestPaint(); }
        }
        Component.onCompleted: requestPaint()
    }
}
