import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import XMF

// Flat modal dialog that builds a form from the tool spec `fields` and runs the
// tool through App.runTool(). Same public API as before.
Item {
    id: sheet
    anchors.fill: parent
    visible: false
    property var toolData: ({})
    property var values: ({})
    property bool running: false

    function open(tool, preset) {
        toolData = tool
        const initial = ({})
        const fields = tool.fields || []
        for (let i = 0; i < fields.length; ++i) {
            const f = fields[i]
            initial[f.key] = (f.default !== undefined && f.default !== null) ? f.default
                                                                          : (f.type === "bool" ? false : "")
        }
        if (preset) {
            for (const key in preset) {
                if (initial[key] !== undefined)
                    initial[key] = preset[key]
            }
        }
        values = initial
        running = false
        visible = true
    }

    function close() { visible = false; running = false }

    function setValue(key, value) {
        const v = ({})
        for (const k in sheet.values) v[k] = sheet.values[k]
        v[key] = value
        sheet.values = v
    }

    function submit() {
        if (sheet.toolData.runnable === false) {
            App.notify("info", "该工具的后端尚未接入")
            return
        }
        const opts = ({})
        const fields = sheet.toolData.fields || []
        for (let i = 0; i < fields.length; ++i) {
            const f = fields[i]
            let v = sheet.values[f.key]
            if (f.type === "int") v = parseInt(v || 0)
            if (f.type === "bool") v = !!v
            opts[f.key] = v
        }
        if (App.runTool(sheet.toolData.id, opts) > 0) {
            sheet.running = true
            closeTimer.restart()
        }
    }

    Timer { id: closeTimer; interval: 400; onTriggered: sheet.close() }

    // dim background
    Rectangle {
        anchors.fill: parent
        color: Theme.darkMode ? "#99000000" : "#66000000"
        visible: sheet.visible
        MouseArea { anchors.fill: parent; onClicked: sheet.close() }
    }

    Rectangle {
        width: Math.min(parent.width - 80, 520)
        height: Math.min(parent.height - 60, formArea.height + 132)
        anchors.centerIn: parent
        visible: sheet.visible
        radius: Theme.r14
        color: Theme.panel
        border.width: 1
        border.color: Theme.borderStrong

        // title bar
        Rectangle {
            id: titleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 34
            color: Theme.panelAlt
            radius: Theme.r14

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.toolData.name || "运行工具"
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.top: parent.top
                anchors.topMargin: 17
                width: parent.width - 40
                text: sheet.toolData.desc || ""
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            Item {
                anchors.right: parent.right
                anchors.top: parent.top
                width: 30; height: 30
                IconImage { anchors.centerIn: parent; name: "x"; size: 12; tint: Theme.textSub }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sheet.close() }
            }
        }

        Flickable {
            id: formView
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top
            anchors.margins: 10
            clip: true
            contentHeight: formArea.height
            ScrollBar.vertical: ScrollBar { }

            Column {
                id: formArea
                width: formView.width - 4
                spacing: 8

                Repeater {
                    model: sheet.toolData.fields || []
                    delegate: Column {
                        id: fieldColumn
                        width: formArea.width
                        spacing: 3
                        property var f: modelData

                        Text {
                            text: (fieldColumn.f.label || fieldColumn.f.key)
                                  + (fieldColumn.f.required === true ? " *" : "")
                            color: fieldColumn.f.required === true ? Theme.text : Theme.textSub
                            font.pixelSize: 11
                        }

                        // bool
                        Row {
                            visible: fieldColumn.f.type === "bool"
                            spacing: 6
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !!sheet.values[fieldColumn.f.key]
                                onToggled: function(value) { sheet.setValue(fieldColumn.f.key, value) }
                            }
                            Text {
                                text: sheet.values[fieldColumn.f.key] ? "是" : "否"
                                color: Theme.textMuted
                                font.pixelSize: 11
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // choice
                        ComboBox {
                            id: combo
                            visible: fieldColumn.f.type === "choice"
                            width: parent.width
                            height: 26
                            model: fieldColumn.f.options || []
                            currentIndex: Math.max(0, (fieldColumn.f.options || []).indexOf(sheet.values[fieldColumn.f.key]))
                            onActivated: sheet.setValue(fieldColumn.f.key, currentText)
                            font.pixelSize: 11
                            background: Rectangle {
                                radius: Theme.r10
                                color: Theme.panel
                                border.width: 1
                                border.color: Theme.border
                            }
                            contentItem: Text {
                                leftPadding: 6
                                text: combo.displayText
                                color: Theme.text
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                            indicator: null
                        }

                        // text / path / dir / hex / int
                        Row {
                            visible: fieldColumn.f.type !== "bool" && fieldColumn.f.type !== "choice"
                            width: parent.width
                            spacing: 6

                            Rectangle {
                                width: parent.width - (browseBtn.visible ? 64 : 0)
                                height: 26
                                radius: Theme.r10
                                color: Theme.panel
                                border.width: 1
                                border.color: editor.activeFocus ? Theme.accent : Theme.border

                                TextField {
                                    id: editor
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    color: Theme.text
                                    font.pixelSize: 11
                                    selectByMouse: true
                                    background: null
                                    placeholderText: fieldColumn.f.type === "path" ? "选择文件…"
                                                   : fieldColumn.f.type === "dir" ? "选择目录…"
                                                   : fieldColumn.f.type === "hex" ? "如 0x65000800"
                                                   : (fieldColumn.f.placeholder || "")
                                    placeholderTextColor: Theme.textMuted
                                    text: sheet.values[fieldColumn.f.key] !== undefined
                                          ? String(sheet.values[fieldColumn.f.key]) : ""
                                    onTextEdited: sheet.setValue(fieldColumn.f.key, text)
                                }
                            }

                            AppButton {
                                id: browseBtn
                                visible: fieldColumn.f.type === "path" || fieldColumn.f.type === "dir"
                                text: "浏览"
                                kind: 1
                                small: true
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: {
                                    if (fieldColumn.f.type === "dir")
                                        folderDlg.open()
                                    else
                                        fileDlg.open()
                                }
                            }
                        }

                        Text {
                            visible: (fieldColumn.f.hint || "") !== ""
                            text: fieldColumn.f.hint || ""
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        FileDialog {
                            id: fileDlg
                            title: fieldColumn.f.label || "选择文件"
                            nameFilters: ["所有文件 (*.*)"]
                            onAccepted: sheet.setValue(fieldColumn.f.key, String(selectedFile).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
                        }
                        FolderDialog {
                            id: folderDlg
                            title: fieldColumn.f.label || "选择目录"
                            onAccepted: sheet.setValue(fieldColumn.f.key, String(selectedFolder).replace(/^file:\/\/\//, "").replace(/\//g, "\\"))
                        }
                    }
                }
            }
        }

        Rectangle {
            id: footer
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 44
            color: Theme.panelAlt
            radius: Theme.r14

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.divider
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                AppButton { text: "取消"; kind: 1; onClicked: sheet.close() }
                AppButton {
                    text: sheet.running ? "已提交" : "运行"
                    kind: 0
                    enabled: !sheet.running && sheet.toolData.runnable !== false
                    onClicked: sheet.submit()
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.toolData.id || ""
                color: Theme.textMuted
                font.pixelSize: 10
            }
        }
    }
}
