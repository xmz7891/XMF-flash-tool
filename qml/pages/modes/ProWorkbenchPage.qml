import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import XMF

// Professional workbench: OPLUS flashing, ROM center, EDL maintenance,
// scrcpy parameters, OCDT and TWRP. Each action opens the generated ToolSheet
// so validation and task progress stay in one place.
Item {
    id: page
    signal backRequested()

    property string tab: "oplus"
    readonly property var tabs: [
        { id: "oplus", name: "欧加刷写", icon: "smartphone" },
        { id: "rom", name: "ROM 资源", icon: "download-cloud" },
        { id: "edl", name: "EDL 维护", icon: "cpu" },
        { id: "scrcpy", name: "投屏", icon: "monitor-smartphone" },
        { id: "ocdt", name: "OCDT", icon: "file-search" },
        { id: "twrp", name: "TWRP", icon: "table" },
        { id: "apps", name: "应用 / 备份", icon: "package" },
        { id: "gki", name: "GKI / AVB", icon: "cpu" }
    ]

    function open(id) {
        sheet.open(App.toolInfo(id))
    }

    Component.onCompleted: {
        if (App.fastbootDevices.length > 0)
            App.refreshDevices()
    }

    Column {
        anchors.fill: parent
        spacing: 10

        PageHeader {
            width: parent.width
            title: "专业工作台"
            subtitle: "欧加刷写 · ROM 资源 · EDL 维护 · 投屏 · OCDT · TWRP"
            icon: "wrench"

            AppButton {
                text: "返回"
                icon: "arrow-left"
                kind: 2
                small: true
                onClicked: page.backRequested()
            }
            AppButton {
                text: "刷新设备"
                icon: "refresh-cw"
                kind: 1
                small: true
                onClicked: { App.scanPorts(); App.refreshDevices() }
            }
        }

        Row {
            width: parent.width
            height: parent.height - 64
            spacing: 10

            // left rail
            GlassSurface {
                width: 190
                height: parent.height
                title: "工具"

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Repeater {
                        model: page.tabs
                        delegate: Rectangle {
                            width: parent.width
                            height: 38
                            radius: Theme.r10
                            color: page.tab === modelData.id ? Theme.alpha(Theme.accent, 0.18)
                                                             : (railMouse.containsMouse ? Theme.panelHover : "transparent")
                            border.width: page.tab === modelData.id ? 1 : 0
                            border.color: Theme.alpha(Theme.accent, 0.45)

                            Row {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                IconImage {
                                    name: modelData.icon
                                    size: 14
                                    tint: page.tab === modelData.id ? Theme.accent : Theme.textSub
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: modelData.name
                                    color: page.tab === modelData.id ? Theme.text : Theme.textSub
                                    font.pixelSize: Theme.fs(11)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                id: railMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.tab = modelData.id
                            }
                        }
                    }
                }
            }

            // right content
            Flickable {
                width: parent.width - 200
                height: parent.height
                clip: true
                contentHeight: content.implicitHeight + 20
                ScrollBar.vertical: ScrollBar { }

                Column {
                    id: content
                    width: parent.width
                    spacing: 10

                    // ---------------- OPLUS ----------------
                    GlassSurface {
                        width: parent.width
                        height: 470
                        visible: page.tab === "oplus"
                        title: "欧加刷写工作台 · OPPO / 一加 / realme"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Text {
                                width: parent.width
                                text: "流程：检查全量包 / 售后包 → 解包 OFP / OZIP / OTA → 生成 Fastboot 刷写计划 → 执行。"
                                      + "写操作需二次确认，默认只刷 boot 类安全分区。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "检查固件包"; icon: "package-search"; small: true; kind: 0; onClicked: page.open("oplus.package.inspect") }
                                AppButton { text: "生成刷写计划"; icon: "list-ordered"; small: true; kind: 1; onClicked: page.open("oplus.flash.plan") }
                                AppButton { text: "Fastboot 刷写"; icon: "flame"; small: true; kind: 1; onClicked: page.open("oplus.flash.run") }
                                AppButton { text: "OTA / 降级检查"; icon: "history"; small: true; kind: 1; onClicked: page.open("oplus.ota.inspect") }
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "OFP / OZIP 解包"; icon: "file-archive"; small: true; kind: 2; onClicked: page.open("firmware.file.unpackOfp") }
                                AppButton { text: "Payload 解包"; icon: "package"; small: true; kind: 2; onClicked: page.open("image.payload.unpack") }
                                AppButton { text: "Super 修复"; icon: "layers"; small: true; kind: 2; onClicked: page.open("image.super.merge") }
                                AppButton { text: "ARB / 熔断检查"; icon: "shield-alert"; small: true; kind: 2; onClicked: page.open("oplus.ota.inspect") }
                            }

                            Rectangle { width: parent.width; height: 1; color: Theme.divider }

                            Text {
                                width: parent.width
                                text: "全量包 / 售后包 / 救砖"
                                color: Theme.text
                                font.pixelSize: Theme.fs(12)
                                font.weight: Font.DemiBold
                            }
                            Grid {
                                columns: 3
                                spacing: 8
                                Repeater {
                                    model: [
                                        { t: "全量包", d: "完整 Fastboot 流程", tool: "oplus.flash.run" },
                                        { t: "售后包", d: "散包 / 指定分区", tool: "oplus.flash.plan" },
                                        { t: "救砖", d: "FastbootD / 恢复", tool: "fastboot.flash" },
                                        { t: "Slot A 精简", d: "只刷 boot 类", tool: "oplus.flash.run" },
                                        { t: "ARB 检测", d: "版本回滚风险", tool: "oplus.ota.inspect" },
                                        { t: "Super 修复", d: "合并动态分区", tool: "image.super.merge" }
                                    ]
                                    delegate: Rectangle {
                                        width: 190; height: 66; radius: Theme.r12
                                        color: Theme.panel
                                        border.width: 1; border.color: Theme.border
                                        Text { x: 10; y: 10; text: modelData.t; color: Theme.text; font.pixelSize: Theme.fs(11); font.weight: Font.DemiBold }
                                        Text { x: 10; y: 32; text: modelData.d; color: Theme.textMuted; font.pixelSize: Theme.fs(9) }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.open(modelData.tool) }
                                    }
                                }
                            }
                        }
                    }

                    // ---------------- ROM ----------------
                    GlassSurface {
                        width: parent.width
                        height: 470
                        visible: page.tab === "rom"
                        title: "ROM 资源中心"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Text {
                                width: parent.width
                                text: "小米 / Redmi 支持在线检索与下载；OPPO / realme / 魅族 / 联想 提供官方入口与通用 URL 下载。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "小米 / Redmi 检索"; icon: "search"; small: true; kind: 0; onClicked: page.open("firmware.xiaomi.search") }
                                AppButton { text: "小米 / Redmi 下载"; icon: "download"; small: true; kind: 0; onClicked: page.open("firmware.xiaomi.download") }
                                AppButton { text: "通用 ROM 下载"; icon: "download-cloud"; small: true; kind: 1; onClicked: page.open("firmware.rom.download") }
                                AppButton { text: "多线程下载"; icon: "download"; small: true; kind: 1; onClicked: page.open("firmware.rom.multidownload") }
                                AppButton { text: "OTA 检查"; icon: "history"; small: true; kind: 1; onClicked: page.open("firmware.xiaomi.otaCheck") }
                            }
                            Rectangle { width: parent.width; height: 1; color: Theme.divider }
                            Text { text: "品牌入口"; color: Theme.text; font.pixelSize: Theme.fs(12); font.weight: Font.DemiBold }
                            Grid {
                                columns: 3
                                spacing: 8
                                Repeater {
                                    model: [
                                        { n: "OPPO", id: "oppo" }, { n: "realme", id: "realme" },
                                        { n: "一加", id: "oneplus" }, { n: "魅族", id: "meizu" },
                                        { n: "联想", id: "lenovo" }, { n: "小米 / Redmi", id: "xiaomi" }
                                    ]
                                    delegate: Rectangle {
                                        width: 190; height: 60; radius: Theme.r12
                                        color: Theme.panel; border.width: 1; border.color: Theme.border
                                        Text { x: 10; y: 10; text: modelData.n; color: Theme.text; font.pixelSize: Theme.fs(12); font.weight: Font.DemiBold }
                                        Text { x: 10; y: 33; text: "官方固件入口 →"; color: Theme.accent; font.pixelSize: Theme.fs(9) }
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                const info = App.firmware.brand(modelData.id)
                                                const links = info.official || []
                                                if (links.length > 0) {
                                                    App.openUrl(links[0].url)
                                                } else if (modelData.id === "meizu") {
                                                    App.openUrl("https://www.flyme.com/")
                                                } else if (modelData.id === "lenovo") {
                                                    App.openUrl("https://www.lenovo.com.cn/")
                                                } else {
                                                    App.notify("info", "该品牌暂无内置官方入口，可用「通用 ROM 下载」")
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                text: "下载支持 HTTP Range 断点续传；大文件建议使用「通用 ROM 下载」并填写直链。"
                                color: Theme.textDim
                                font.pixelSize: Theme.fs(9)
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // ---------------- EDL ----------------
                    GlassSurface {
                        width: parent.width
                        height: 430
                        visible: page.tab === "edl"
                        title: "EDL 专用维护 · Qualcomm 9008"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "复用 XMF 的 Firehose / qedl 后端。GPT 写入和格式化 LUN 属于危险操作，必须二次确认。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Grid {
                                columns: 3
                                spacing: 8
                                Repeater {
                                    model: [
                                        { t: "基带 / 指纹备份", d: "modem / persist 预设", tool: "qcom.maint.backup" },
                                        { t: "写入 GPT", d: "完整 GPT 镜像", tool: "qcom.maint.gpt.write" },
                                        { t: "格式化 LUN0", d: "按 GPT 擦除非保护分区", tool: "qcom.maint.format.lun" },
                                        { t: "读取 GPT", d: "显示分区表", tool: "qcom.flow.partitions" },
                                        { t: "分区备份", d: "自选分区", tool: "qcom.flow.backup" },
                                        { t: "重启 / 切槽", d: "EDL 维护", tool: "qcom.flow.reboot" }
                                    ]
                                    delegate: Rectangle {
                                        width: 190; height: 82; radius: Theme.r12
                                        color: Theme.panel; border.width: 1; border.color: Theme.border
                                        Text { x: 10; y: 10; text: modelData.t; color: Theme.text; font.pixelSize: Theme.fs(11); font.weight: Font.DemiBold }
                                        Text { x: 10; y: 32; text: modelData.d; color: Theme.textMuted; font.pixelSize: Theme.fs(9); width: 170; wrapMode: Text.Wrap }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.open(modelData.tool) }
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                text: "强开 OEM / 解锁：请在 Bootloader 使用「OEM 解锁 / 回锁」工具；不同厂商策略不同。"
                                color: Theme.textDim
                                font.pixelSize: Theme.fs(9)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "打开 Bootloader OEM 工具"; icon: "unlock"; small: true; kind: 2; onClicked: page.open("fastboot.oem") }
                                AppButton { text: "强开 OEM（厂商策略）"; icon: "unlock-keyhole"; small: true; kind: 2; onClicked: page.open("oem.unlock.strategy") }
                            }
                        }
                    }

                    // ---------------- scrcpy ----------------
                    GlassSurface {
                        width: parent.width
                        height: 380
                        visible: page.tab === "scrcpy"
                        title: "scrcpy 参数面板"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "分辨率 / 帧率 / 码率 / 标题 / 虚拟按键 / 保持唤醒 / 录制。启动后使用 scrcpy 自带窗口。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "参数投屏"; icon: "monitor-smartphone"; small: true; kind: 0; onClicked: page.open("scrcpy.launch") }
                                AppButton { text: "快速投屏"; icon: "play"; small: true; kind: 1; onClicked: App.officialTools.launch("scrcpy") }
                                AppButton { text: "刷新设备"; icon: "refresh-cw"; small: true; kind: 2; onClicked: App.refreshDevices() }
                            }
                            Text { text: "默认：1600 宽 · 60 FPS · 8 Mbps · 无音频 · 保持唤醒"; color: Theme.textDim; font.pixelSize: Theme.fs(10) }
                        }
                    }

                    // ---------------- OCDT ----------------
                    GlassSurface {
                        width: parent.width
                        height: 360
                        visible: page.tab === "ocdt"
                        title: "OPLUS OCDT"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "分析和生成 OCDT 配置。生成的是可读 XML 模板，正式刷机前需与官方 OCDT 对照。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "分析 OCDT"; icon: "file-search"; small: true; kind: 0; onClicked: page.open("oplus.ocdt.inspect") }
                                AppButton { text: "生成模板"; icon: "file-plus"; small: true; kind: 1; onClicked: page.open("oplus.ocdt.generate") }
                            }
                        }
                    }

                    // ---------------- GKI / AVB ----------------
                    GlassSurface {
                        width: parent.width
                        height: 430
                        visible: page.tab === "gki"
                        title: "GKI 制作 / AVB 签名"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "从 boot 提取 GKI Image，换回 boot；AVB 签名调用 AOSP avbtool，需要私钥与分区大小。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "GKI 解包"; icon: "cpu"; small: true; kind: 0; onClicked: page.open("image.gki.unpack") }
                                AppButton { text: "GKI 重打包"; icon: "package"; small: true; kind: 1; onClicked: page.open("image.gki.pack") }
                                AppButton { text: "AVB 签名"; icon: "shield-check"; small: true; kind: 1; onClicked: page.open("image.avb.sign") }
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "VBMeta 信息"; icon: "info"; small: true; kind: 2; onClicked: page.open("image.vbmeta.info") }
                                AppButton { text: "关闭校验"; icon: "shield-off"; small: true; kind: 2; onClicked: page.open("image.vbmeta.disable") }
                                AppButton { text: "DM-verity 校验"; icon: "shield"; small: true; kind: 2; onClicked: page.open("image.vbmeta.verify") }
                            }
                            Text {
                                width: parent.width
                                text: "AVB 签名需要 avbtool；未安装时会明确报错，不会伪造签名。"
                                color: Theme.textDim
                                font.pixelSize: Theme.fs(9)
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // ---------------- APPS / BACKUP ----------------
                    GlassSurface {
                        width: parent.width
                        height: 430
                        visible: page.tab === "apps"
                        title: "应用管理 / 数据备份"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "冻结 / 解冻 / 卸载应用、提取 APK、安装 APK，以及相册 / 视频 / 通讯录备份。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "冻结 / 解冻 / 卸载"; icon: "package-x"; small: true; kind: 0; onClicked: page.open("adb.app.control") }
                                AppButton { text: "提取 APK"; icon: "file-down"; small: true; kind: 1; onClicked: page.open("adb.apk.extract") }
                                AppButton { text: "安装 APK"; icon: "download"; small: true; kind: 1; onClicked: page.open("adb.install") }
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "相册 / 视频备份"; icon: "images"; small: true; kind: 2; onClicked: page.open("adb.media.backup") }
                                AppButton { text: "通讯录导出"; icon: "contact"; small: true; kind: 2; onClicked: page.open("adb.contacts.backup") }
                                AppButton { text: "设备信息"; icon: "info"; small: true; kind: 2; onClicked: page.open("adb.info") }
                            }
                            Text {
                                width: parent.width
                                text: "应用管理依赖 ADB 调试授权；通讯录导出需要设备授权 content provider。"
                                color: Theme.textDim
                                font.pixelSize: Theme.fs(9)
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // ---------------- TWRP ----------------
                    GlassSurface {
                        width: parent.width
                        height: 400
                        visible: page.tab === "twrp"
                        title: "TWRP 分区工作台"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "通过 ADB 在 TWRP Recovery 中 dd 读取 / 写回 by-name 分区。写入与擦除都有保护与确认。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fs(11)
                                wrapMode: Text.Wrap
                            }
                            Row {
                                spacing: 8
                                AppButton { text: "读取分区"; icon: "table"; small: true; kind: 0; onClicked: page.open("twrp.partitions") }
                                AppButton { text: "备份"; icon: "save"; small: true; kind: 1; onClicked: page.open("twrp.backup") }
                                AppButton { text: "恢复"; icon: "upload"; small: true; kind: 2; onClicked: page.open("twrp.restore") }
                                AppButton { text: "擦除"; icon: "eraser"; small: true; kind: 2; onClicked: page.open("twrp.erase") }
                            }
                            Text {
                                width: parent.width
                                text: "提示：TWRP 需支持 /dev/block/by-name 与 dd；系统分区在保护列表内，禁止擦除。"
                                color: Theme.textDim
                                font.pixelSize: Theme.fs(9)
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    ToolSheet { id: sheet; anchors.fill: parent }
}
