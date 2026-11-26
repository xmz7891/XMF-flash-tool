import QtQuick
import QtQuick.Controls.Basic
import XMF

// Dark popup used by the command bar. The default Basic Menu is light and
// looks out of place against the instrument-panel shell.
Menu {
    id: root
    padding: 6
    width: Math.max(230, implicitWidth)

    background: Rectangle {
        color: Theme.panel
        radius: 12
        border.width: 1
        border.color: Theme.borderStrong
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durFast; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durFast }
    }
}
