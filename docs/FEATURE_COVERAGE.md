# 功能覆盖矩阵（对照 code_20261108.csv）

状态标记：✅ 已实现并有工具/页面入口 · 🟡 已实现核心路径，边界功能待补 · ⛔ 未实现（设备/授权/数据依赖）

| # | CSV 功能大类 | 实现位置 | 入口 | 状态 |
| --- | --- | --- | --- | --- |
| 01 | 固件 & 镜像全套工具 | `src/image`（51 个工具） | 工具库 → 固件 & 镜像 | 🟡 payload/boot/init_boot/vendor_boot/cpio/super/sparse/vbmeta/GPT/ELF/压缩/zip/开机 LOGO 提取与打包/zstd 解压/payload 增量结构分析已实现；AVB 签名、OFP/OZIP 加密容器（需要厂商密钥）不做 |
| 02 | 高通 9008 工具组 | `src/qcom` + `src/flows/QcomFlow` + `src/usb` | 工具库 → 高通 9008 / 刷机中心 | 🟡 Sahara 握手、Firehose 加载、分区表/备份/读写/擦除/A-B 槽/校验已实现；RPMB、SPI、坏块、DIAG 未实现 |
| 03 | MTK 联发科底层 | `src/mtk` + `src/flows/MtkFlow` | 工具库 → 联发科底层 | 🟡 BROM/DA/preloader/scatter 解析与刷写已实现；Kamakiri 漏洞、seccfg、LK 修补、OFP/OZIP、META 未实现 |
| 04 | Bootloader / BL 引导链 | `src/extras`（BootloaderTools/UnlockTools） | 工具库 → Bootloader | 🟡 解锁/上锁、槽位、vbmeta 修补、启动链检查、getvar/分区只读备份、解锁红字检测与官方恢复说明已实现；红字移除与 cmdline 修改需机型专属实现 |
| 05 | 紫光展锐 | `src/sprd` + `src/flows/SprdFlow` | 工具库 → 紫光展锐 | 🟡 FDL1/FDL2 握手、PAC 解包、分区读写/备份/擦除已实现；META、NV/SN/MAC 未实现 |
| 06 | 瑞芯微 RK | `src/rk` + `src/flows/RkFlow` | 工具库 → 瑞芯微 RK | 🟡 MaskROM/Loader 握手、扇区读写、GPT、擦除、RKFW/RKAF 固件解析与解包已实现；IMG 打包与 MAC 写入未实现 |
| 07 | 全志 A 系列 | `src/aw` + `src/flows/AwFlow` | 工具库 → 全志 A 系列 | 🟡 FEL 握手、读写/执行、分区表/备份/恢复、IMAGEWTY（PhoenixSuit）固件解析与解包已实现；分区表修复待补 |
| 08 | Root / KSU / APatch / Magisk | `src/extras/RootTools` | 工具库 → Root / KSU / 面具 | 🟡 boot/init_boot 修补、模块列表/root 检测、Root 痕迹检测与隐藏指引、APatch 内核检查与 kernel/ramdisk 备份已实现；实际 APatch/隐藏配置需在对应管理器内完成 |
| 09 | 账号锁 / 清除类 | `src/extras/UnlockTools` | 工具库 → 账号锁 / 清除 | 🟡 FRP 擦除、userdata/metadata/cache/dalvik 清除已实现；账号锁/SIM 锁提供官方办理流程说明（不绕过验证） |
| 10 | ADB / Fastboot 拓展 | `src/device`（29 个工具）+ 内置 platform-tools | 工具库 → ADB / Fastboot | ✅ 设备/分区/getvar/刷写/擦除/重启/无线调试/信息导出/控制台/bugreport/dmesg/应用冻结与卸载/APK 导出/硬件信息报告已实现；Fastboot 分区读取/变量备份/分区表 CSV 导出/电池检测/刷写前检查（分区容量+SHA256+电量，真机 boot_a 96MB 与拼错分区拒绝）已验证；实际写入待电量恢复后验证 |
| 11 | 固件资源工具 | `src/firmware` + 固件资源页 + `packets.*` | 固件资源 / 工具库 → 固件资源 | 🟡 离线机型库、小米检索/下载/OTA、文件识别/解包/校验/分包合并已实现；三个 GeekFlashPacket 已验证（QC Loader 448.8KB / MTK DA1 581.3KB 可从 zip 内解出，SPRD 包仅含 Config 无引导文件）；其它品牌在线源依赖公开接口；OZIP/OFP 中 zip 兼容的包可直接解包，加密包给出明确说明 |
| 12 | PC 端批量 & 自动化 | `src/device/BatchJobManager` + 批量任务页 | 批量任务 | 🟡 多文件并发（1–4，GUI 与 `--batch-tool` CLI 均可，支持单文件/多文件参数工具）、多设备并行命令、批量编码转换、批量时间戳、重试、取消、进度、日志导出、诊断报告、环境自检已实现；批量多设备并行刷写需真机验证 |
| 13 | 分机型旁路 / 工程 | `src/extras/BypassTools` | 工具库 → 机型旁路 / 工程 | 🟡 通用旁路框架与机型参数读取已实现；"每机型单独工具"需按机型补数据 |
| 14 | 内置 MiFlash 模式 | `qml/pages/modes/MiflashModePage` + `flows/MiFlashFlow` | 内置模式 | ✅ 界面/解析/模式选项 + 真实 Fastboot 刷写后端（tgz 解包 → 按顺序刷写 → 清数据/锁 BL/重启）；仅真机写验证待做 |
| 15 | 内置 SP Flash Tool 模式 | `qml/pages/modes/SpFlashModePage` + `mtk.flow.flash` | 内置模式 / 官方工具（内嵌） | 🟡 界面/Scatter 解析 + MTK 流水线已接线；BROM 真机刷写待验证；官方 flash_tool 可内嵌运行 |
| 16 | 内置 SPD Flash Tool 模式 | `qml/pages/modes/SpdFlashModePage` + `sprd.flow.flash` | 内置模式 | 🟡 PAC 解析 + 展锐流水线已接线；FDL 真机刷写待验证 |
| 17 | 内置 RKDevTool 模式 | `qml/pages/modes/RkDevToolModePage` | 内置模式 | 🟡 界面 + RK 固件解析（RKFW/RKAF → 分区表）已接入；IMG 打包未实现 |
| 18 | 内置 PhoenixSuit 模式 | `qml/pages/modes/PhoenixSuitModePage` | 内置模式 | 🟡 FEL 流程 + IMAGEWTY 固件解析（文件表 → 分区列表）已接入 |

## 说明

* "工具库"中每个工具都带 `runnable` 标记，无后端的工具会在弹窗里明确提示，不会伪装成功。
* 涉及擦除/写入的操作全部需要二次确认；`unlock`/`frp` 等有风险工具单独分类。
* 内置官方工具（MiFlash、SP Flash Tool、Research Download）以子窗口方式嵌入主界面，
  不弹独立窗口；它们与内置模式页面互不影响。
* 真机验收状态见 `docs/DEVICE_LAYER.md` 与仓库内 `build/final_*.png` 截图。
