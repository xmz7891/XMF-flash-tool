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
        sourceComponent: root.pageComp
        onLoaded: {
            if (!item)
                return
            const props = root.pageProps || ({})
            for (const k in props)
                item[k] = props[k]
        }
    }
}
