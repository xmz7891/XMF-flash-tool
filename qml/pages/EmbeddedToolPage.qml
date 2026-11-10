import QtQuick
import QtQuick.Controls.Basic
import XMF

// Runs an official vendor tool inside the XMF window. The native child window
// is positioned over `nativeHost`; if the tool refuses to grow/shrink to the
// available area we letterbox it instead of cropping it, which keeps the layout
// calm instead of "sticking out" over our UI.
Item {
    id: page
    property string toolId: ""
    property string toolName: "官方工具"
    property string toolExe: ""
    property string toolDir: ""
    property bool started: false
    property bool showBack: true

    signal backRequested()

    function hostRect() {
        return nativeHost.mapToItem(null, 0, 0)
    }

    function resolveTool() {
        const tools = App.officialTools.tools
        for (let i = 0; i < tools.length; ++i) {
            if (tools[i].id === page.toolId) {
                page.toolName = tools[i].name
                page.toolExe = tools[i].path
                page.toolDir = tools[i].dir
                return true
            }
        }
        return false
    }

    function prepareAndLaunch() {
        if (page.toolId === "")
            return
        if (!resolveTool() || page.toolExe === "") {
            App.notify("error", "找不到官方工具：" + page.toolId)
            return
        }
        if (!launchTimer.running)
            launchTimer.start()
    }

    function launch() {
        if (page.toolExe === "") {
            App.notify("error", "找不到官方工具：" + page.toolId)
            return
        }
        const area = hostRect()
        page.started = App.embedded.start(page.toolExe, [], page.toolDir,
                                          Math.round(area.x), Math.round(area.y),
                                          Math.round(nativeHost.width), Math.round(nativeHost.height))
        if (!page.started)
            App.notify("error", "启动失败：" + App.embedded.error)
    }

    function updateArea() {
        const area = hostRect()
        App.embedded.updateArea(Math.round(area.x), Math.round(area.y),
                                Math.round(nativeHost.width), Math.round(nativeHost.height))
    }

    // toolId is injected by PageHost after the component is created, so start
    // the vendor app from onToolIdChanged as well as onCompleted.
    onToolIdChanged: page.prepareAndLaunch()
    Component.onCompleted: page.prepareAndLaunch()

    Timer {
        id: launchTimer
        interval: Theme.animations ? 240 : 0
        onTriggered: page.launch()
    }
    Component.onDestruction: App.embedded.close()

    Column {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 10

        Row {
            width: parent.width
            height: 44
            spacing: 12

            AppButton {
                visible: page.showBack
                text: "返回"
                icon: "arrow-left"
                kind: 2
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    App.embedded.close()
                    page.backRequested()
                }
            }

            Rectangle {
                width: 30; height: 30
                radius: 9
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.alpha(Theme.text, 0.06)
                border.width: 1
                border.color: Theme.border
                IconImage {
                    anchors.centerIn: parent
                    name: "box"
                    size: 15
                    tint: Theme.textSub
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: page.toolName
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    text: App.embedded.running
                          ? (App.embedded.embedded ? "已内嵌运行 · " + App.embedded.title : "正在启动官方工具…")
                          : "未运行"
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }

            Item { width: Math.max(0, parent.width - 620); height: 1 }

            AppButton {
                text: "重新启动"
                icon: "refresh-cw"
                kind: 1
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    App.embedded.close()
                    Qt.callLater(page.launch)
                }
            }
            AppButton {
                text: "独立窗口"
                icon: "external-link"
                kind: 1
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: App.embedded.detachToDesktop()
            }
            AppButton {
                text: "关闭工具"
                icon: "x"
                kind: 2
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: App.embedded.close()
            }
        }

        // available area; the native window is letterboxed inside it
        GlassSurface {
            id: frame
            width: parent.width
            height: parent.height - 76
            radius: 0
            color: Theme.darkMode ? "#0A0A0C" : Theme.panelAlt
            border.color: Theme.border
            clip: true

            Item {
                id: nativeHost
                // default: fill the frame; once the tool reports its content size
                // and cannot fill it, centre a correctly sized host instead of
                // cropping the vendor window.
                width: App.embedded.childSizeValid
                       ? Math.min(frame.width - 24, Math.max(640, App.embedded.childWidth))
                       : frame.width - 2
                height: App.embedded.childSizeValid
                        ? Math.min(frame.height - 24, Math.max(420, App.embedded.childHeight))
                        : frame.height - 2
                anchors.centerIn: parent

                Behavior on width { NumberAnimation { duration: Theme.durNormal; easing.type: Theme.easing } }
                Behavior on height { NumberAnimation { duration: Theme.durNormal; easing.type: Theme.easing } }

                onWidthChanged: Qt.callLater(page.updateArea)
                onHeightChanged: Qt.callLater(page.updateArea)
                onXChanged: Qt.callLater(page.updateArea)
                onYChanged: Qt.callLater(page.updateArea)
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: !App.embedded.embedded
                IconImage {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "box"
                    size: 22
                    tint: Theme.textMuted
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: App.embedded.running ? "正在启动官方工具，请稍候…" : "官方工具未运行"
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
            }
        }

        Row {
            width: parent.width
            height: 18
            spacing: 8
            Text {
                text: App.embedded.error !== "" ? App.embedded.error
                      : "官方工具以子窗口方式内嵌在主界面中；可随时切换到独立窗口或关闭。"
                color: App.embedded.error !== "" ? Theme.danger : Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
