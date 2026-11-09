import QtQuick
import XMF
import QtQuick.Effects

Item {
    id: root
    property string name: ""
    property color tint: Theme.text
    property int size: 20
    property real iconOpacity: 1.0

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name.length > 0 ? "qrc:/icons/white/" + root.name + ".svg" : ""
        sourceSize.width: Math.round(root.size * 4)
        sourceSize.height: Math.round(root.size * 4)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: img
        colorization: 1.0
        colorizationColor: root.tint
        opacity: root.iconOpacity
        autoPaddingEnabled: false
    }
}
