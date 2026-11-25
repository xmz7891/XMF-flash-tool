import QtQuick
import XMF

// Opaque host for pages pushed onto the StackView. Having a real background
// lets the stack cross-fade pages without showing the page underneath.
Item {
    id: root
    property Component pageComp: null
    property var pageProps: ({})

    Rectangle {
        anchors.fill: parent
        color: Theme.windowBg
    }

    Loader {
        id: loader
        anchors.fill: parent
        asynchronous: true
        sourceComponent: root.pageComp
        opacity: 0
        scale: 0.995
        Behavior on opacity { NumberAnimation { duration: Theme.animations ? 180 : 0; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Theme.animations ? 220 : 0; easing.type: Easing.OutCubic } }
        onStatusChanged: {
            if (status === Loader.Ready) {
                opacity = 1
                scale = 1
            }
        }
        onLoaded: {
            if (!item)
                return
            const props = root.pageProps || ({})
            for (const k in props)
                item[k] = props[k]
        }
    }
}
