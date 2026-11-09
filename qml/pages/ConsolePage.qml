import QtQuick
import QtQuick.Controls.Basic
import XMF

// ADB / Fastboot command console.
Item {
    id: page

    property int mode: 0            // 0 = ADB shell, 1 = Fastboot
    property string serial: ""
    property var history: []
    property int historyIndex: -1

    function devices() {
        return mode === 0 ? App.adbDevices : App.fastbootDevices
    }

    function currentSerial() {
        if (serial.length > 0)
            return serial
        const list = devices()
        return list.length > 0 ? String(list[0].serial || "") : ""
    }

    function append(text) {
        outputArea.text += text + "\n"
        consoleView.positionViewAtEnd()
    }

    function run() {
        const command = commandField.text.trim()
        if (command.length === 0)
            return
        history = [command].concat(history.slice(0, 49))
        historyIndex = -1
        const begin = Date.now()
        const result = mode === 0
                       ? App.runAdbCommand(page.currentSerial(), command)
                       : App.runFastbootCommand(page.currentSerial(), command)
        const elapsed = Date.now() - begin
        const prefix = mode === 0 ? "$ adb shell " : "$ fastboot "
        append(prefix + command)
        if (result.output && result.output.length > 0)
            append(String(result.output).replace(/\s+$/, ""))
        if (result.error && result.error.length > 0)
            append("[错误] " + result.error)
        append("(" + elapsed + " ms" + (result.ok ? "" : " · 失败") + ")")
        append("")
        commandField.text = ""
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 6

        PageHeader {
            width: parent.width
            title: "控制台"
            subtitle: "直接执行 ADB shell / Fastboot 命令 · 输出实时显示"
            icon: "terminal"
            AppButton {
                text: "清空输出"
                kind: 1
                small: true
                onClicked: outputArea.text = ""
            }
        }

        Row {
            width: parent.width
            height: 30
            spacing: 6

            AppButton {
                text: "ADB"
                kind: page.mode === 0 ? 0 : 1
                small: true
                onClicked: { page.mode = 0; page.serial = "" }
            }
            AppButton {
                text: "Fastboot"
                kind: page.mode === 1 ? 0 : 1
                small: true
                onClicked: { page.mode = 1; page.serial = "" }
            }

            Text {
                text: "设备"
                color: Theme.textMuted
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            ComboBox {
                id: deviceCombo
                width: 220
                height: 26
                anchors.verticalCenter: parent.verticalCenter
                model: page.devices()
                textRole: "serial"
                currentIndex: page.serial.length > 0 ? -1 : 0
                onActivated: if (index >= 0 && index < model.length)
                                 page.serial = String(model[index].serial || "")
                font.pixelSize: 11
                background: Rectangle {
                    radius: Theme.r10
                    color: Theme.panel
                    border.width: 1
                    border.color: Theme.border
                }
                indicator: IconImage { x: deviceCombo.width - 18; y: 7; name: "chevron-down"; size: 12; tint: Theme.textMuted }
                contentItem: Text {
                    leftPadding: 6
                    text: deviceCombo.count > 0 && deviceCombo.currentIndex >= 0
                          ? String(deviceCombo.model[deviceCombo.currentIndex].serial || "—")
                          : (page.serial.length > 0 ? page.serial : "自动选择")
                    color: Theme.text
                    font.pixelSize: 11
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            Text {
                text: page.mode === 0 ? "命令（shell 后的部分）" : "参数（fastboot 后的部分）"
                color: Theme.textMuted
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                width: parent.width - 700
                height: 26
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.r10
                color: Theme.panel
                border.width: 1
                border.color: commandField.activeFocus ? Theme.accent : Theme.border

                TextField {
                    id: commandField
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    color: Theme.text
                    font.family: "Consolas"
                    font.pixelSize: 11
                    selectByMouse: true
                    background: null
                    placeholderText: page.mode === 0 ? "getprop ro.product.model"
                                                     : "getvar all"
                    placeholderTextColor: Theme.textMuted
                    onAccepted: page.run()
                }
            }
            AppButton {
                text: "运行"
                kind: 0
                small: true
                anchors.verticalCenter: parent.verticalCenter
                enabled: commandField.text.trim().length > 0
                onClicked: page.run()
            }
        }

        Row {
            width: parent.width
            spacing: 6
            Repeater {
                model: page.mode === 0
                       ? ["getprop ro.product.model", "getprop ro.build.version.release", "getprop ro.build.version.sdk",
                          "pm list packages -3 | head -n 20", "dumpsys battery", "ip addr show wlan0"]
                       : ["getvar all", "getvar current-slot", "getvar partition-size:boot_a",
                          "oem device-info", "getvar battery-voltage"]
                delegate: AppButton {
                    text: modelData
                    kind: 1
                    small: true
                    onClicked: { commandField.text = modelData; page.run() }
                }
            }
        }

        GlassSurface {
            width: parent.width
            height: parent.height - 120
            title: "输出"

            Flickable {
                id: consoleView
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                contentHeight: outputArea.height
                contentWidth: outputArea.width
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                TextArea {
                    id: outputArea
                    width: consoleView.width
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    selectByMouse: true
                    color: Theme.text
                    font.family: "Consolas"
                    font.pixelSize: 11
                    background: null
                    text: "# XMF 命令控制台\n# ADB / Fastboot 命令会直接发送到当前设备\n\n"
                }
            }
        }
    }
}
