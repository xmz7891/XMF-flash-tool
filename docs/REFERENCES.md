# 参考开源项目与 XMF 的落地情况

本文件对应《刷机工具开发 · 参考项目清单》（`刷机工具开发-参考项目清单.xlsx`），逐项说明我们借鉴了什么、
在 XMF 里落地到哪里，以及哪些仍是后续计划。XMF 的所有底层实现均为原生 C++/Qt（不捆绑 Python/.NET
运行时），官方厂商工具作为可选后端随包附带。

| # | 参考项目 | 借鉴的核心点 | 在 XMF 中的落地 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | [PixelFlasher](https://github.com/badabing2005/PixelFlasher) | GUI 封装 adb/fastboot、线刷包解析、日志与进度条 | `src/device/AdbClient`、`FastbootClient`、`TaskManager`、`LogModel`；MiFlash 工作台（`qml/pages/modes/MiflashModePage.qml`） | 已实现 |
| 2 | [OpenAndroidInstaller](https://github.com/openandroidinstaller-dev/openandroidinstaller) | 向导式刷机流程、错误提示体系 | `FlashCenterPage` 平台卡片 + 工作台步骤条 + 中文错误码提示 | 已借鉴 |
| 3 | [柚坛工具箱 UotanToolbox](https://github.com/Uotan-Dev/UotanToolboxNT) | payload 解包、super 动态分区、批量刷写 | `src/image/Payload`、`SuperImage`、`src/device/BatchJobManager` | 已实现（独立 C++ 实现） |
| 4 | [QtScrcpy](https://github.com/barry-ran/QtScrcpy) | Qt 下 USB 热插拔、adb 子进程、多线程模型 | `src/device/PortsMonitor`（WM_DEVICECHANGE）、`QProcess` 封装、`QThreadPool` 任务引擎 | 已借鉴 |
| 5 | [scrcpy](https://github.com/Genymobile/scrcpy) | adb 服务端与调用链路 | `AdbClient`（devices/shell/push/pull/forward/logcat） | 已借鉴 |
| 6 | [mtkclient](https://github.com/bkerler/mtkclient) | BROM/Preloader 握手、DA 加载、scatter、漏洞注入、seccfg | `src/mtk/Brom`、`Da`、`Preloader`、`Scatter`；`src/flows/MtkFlow`；seccfg/protect 分区读写 | 核心已实现；kamakiri/amonet/hashimoto 漏洞注入列为实验性 TODO |
| 7 | [edl (bkerler)](https://github.com/bkerler/edl) | Sahara 握手、Firehose 加载、分区读写/dump | `src/qcom/Sahara`、`Firehose`、`Elf`；官方 `QSaharaServer.exe`/`fh_loader.exe` 集成为官方后端流水线 | 已实现 |
| 8 | [payload-dumper-go](https://github.com/ssut/payload-dumper-go) | payload.bin 解包、分区导出、哈希校验 | `src/image/Payload`（minimal protobuf 解析、流式导出、SHA256 校验） | 已实现 |
| 9 | [payload_dumper](https://github.com/vm03/payload_dumper) | 同上（结构对照） | 同上 | 已实现 |
| 10 | [Android_boot_image_editor](https://github.com/cfig/Android_boot_image_editor) | boot 解包、ramdisk 改文件、重打包、AVB 重签 | `src/image/BootImage`、`Cpio`、`Vbmeta`；`src/extras/RootImage` 的 KSU/Magisk 修补 | 已实现；AVB 重签（自定义密钥）计划中 |
| 11 | [avbtool (AOSP)](https://android.googlesource.com/platform/external/avb/) | vbmeta/AVB 结构与签名 | `src/image/Vbmeta` 解析、descriptor、flags 修补、hashtree 根哈希校验 | 已实现；签名生成计划（Windows CNG RSA-PSS） |
| 12 | [libusb](https://github.com/libusb/libusb) | 跨平台 USB 通信 | `src/usb/WinUsbTransport`（SetupAPI + WinUSB，原生实现）；libusb 动态加载后备通道 | WinUSB 已实现；libusb 后备列入计划 |
| 13 | [rkdeveloptool](https://github.com/rockchip-linux/rkdeveloptool) | RockUSB CBW/CSW、Loader、读写擦除 | `src/rk`（CBW/CSW、设备信息、读写/擦除/LBA） | 已实现 |
| 14 | [Heimdall](https://github.com/Benjamin-Dobell/Heimdall) | 三星 Odin 协议分包/握手 | —（当前不在 CSV 平台清单内） | 后续计划 |
| 15 | [Android Platform-Tools](https://developer.android.com/tools/releases/platform-tools) | 内置 adb/fastboot 环境 | `data/platform-tools/`（adb/fastboot 37.0.1）+ `PlatformTools` 查找顺序 | 已内置 |
| 16 | [AOSP fastboot](https://android.googlesource.com/platform/system/core/+/refs/heads/main/fastboot/) | fastboot 命令与 USB 传输实现 | 目前调用内置 `fastboot.exe`；协议直连计划 | 已使用官方二进制 |
| 17 | MiFlash | 布局、三种刷机模式（删除/保留/删除并锁 BL） | 内置 MiFlash 工作台 + 官方 `XiaoMiFlash.exe` 兼容后端 | 已实现 |
| 18 | SP Flash Tool | Scatter 解析、Download/Format/Read Back | 内置 SP Flash 工作台（`App.workspace.loadScatter` 动态分区表）+ 官方 `flash_tool.exe` 后端 | 已实现 |
| 19 | Unisoc Research Download | PAC 解析与刷写流程 | 内置 SPD 工作台（`App.workspace.loadPac`）+ 官方 `SPRDClientCore.exe` 后端 | 已实现 |
| 20 | RKDevTool | MaskROM/Loader 状态、分区勾选烧录 | 内置 RKDevTool 工作台 + `src/rk` 协议 | 已实现（固件容器解析补齐中） |
| 21 | PhoenixSuit | FEL 模式烧写、分区擦除 | 内置 PhoenixSuit 工作台 + `src/aw` FEL 协议 | 已实现 |
| 22 | [Qt 6 windeployqt](https://doc.qt.io/qt-6/windows-deployment.html) | 发布时复制 Qt 运行库 | `scripts/package.ps1`（windeployqt --qmldir --compiler-runtime） | 已使用 |
| 23 | [VC++ 运行库](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist) | Qt 运行库依赖 | 采用 MinGW-w64 工具链构建，随包附带 libgcc/libstdc++/winpthread，无需 VC++ Redist | 已规避 |

## 官方厂商工具（随包内置）

`data/thirdparty/` 下内置三套官方工具，作为独立后端可直接启动（详见 `docs/OFFICIAL_TOOLS.md`）：

* 小米 MiFlash（含 Qualcomm `QSaharaServer.exe`、`fh_loader.exe`、MTK AllInOne DA、官方驱动）
* 联发科 SP Flash Tool v5.2404
* 紫光展锐 Unisoc Research Download

## 说明

* 上述开源项目仅用于学习协议与交互设计，XMF 未直接链接其源码；协议实现按公开文档/抓包行为独立编写，
  测试使用 MockTransport 与真实样本（DA/Preloader/devprg ELF）。
* 各依赖的许可证见 `THIRD-PARTY-NOTICES.txt`（随发布包附带）。


## 刷机中心模式分流与参考项目对应

Fastboot / Recovery / 9008(EDL) 是三种不同协议，刷机中心按平台拆分后对应实现如下：

| 模式 | 参考项目 / 官方资料 | 本工具实现 |
| --- | --- | --- |
| Fastboot 工作台 | PixelFlasher、AOSP fastboot、platform-tools | `FastbootModePage` + `fastboot.*` 工具 |
| Recovery 卡刷工作台 | PixelFlasher、OpenAndroidInstaller | `RecoveryModePage` + `adb.sideload` / `adb.waitDevice` |
| Qualcomm 9008 工作台 | bkerler/edl、QSaharaServer/fh_loader | `QualcommEdlModePage` + `qcom.sahara.*` / `qcom.firehose.*` |
| MTK BROM / DA（SP Flash） | mtkclient、SP Flash Tool | `SpFlashModePage` + `mtk.*` 工具 |
| Unisoc U2S / FDL | Unisoc Research Download、SPD 工具链 | `SpdFlashModePage` + `sprd.*` 工具 |
| RK MaskROM / Loader | rkdeveloptool、RKDevTool | `RkDevToolModePage` + `rk.*` 工具 |
| 全志 FEL | PhoenixSuit、sunxi-fel 资料 | `PhoenixSuitModePage` + `aw.fel.*` 工具 |
