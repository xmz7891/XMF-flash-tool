## v0.4.0

- Qualcomm 9008 真机链路修复：识别 eMMC 512 / UFS 4096，自动探测 GPT 扇区并修正读写
- 内置开源 qedl 后端（baiyao105/qedl-rs），进入 9008 后自动上传 Sahara loader
- 修复 Firehose read 的 ACK/原始数据分包，分区读取不再写入 XML ACK 或全零
- 分区恢复写入改为 4096 字节分块，恢复后 SHA256 可校验通过
- 新增 9008 工作台：GPT、LUN 分组、XML 加载/生成、镜像目录匹配、备份/恢复/擦除/重启
- 新增 Amlogic 平台识别、USB Burning 工作台、镜像解析/解包/重打包、update.exe 后端与 Zadig
- 新增 qedl GPT / dump 工具
- 全局字体按窗口缩放，页面菜单动画、底部裁切和模式页自适应修复
- 版本号更新为 0.4.0

## v0.3.1

- 修复全志 FEL 在 A133 等设备上的握手超时
- 补全 32 字节 AWUC USB 请求头与 16 字节 FEL 请求
- 修正 FEL_READ / FEL_WRITE / FEL_EXEC 命令码与字段顺序
- 独立消费 13 字节 AWUS ACK 和 8 字节 FEL status
- 修正 AWUSBFEX 的 SoC ID / 协议版本解析，新增 A133
- FEL 失败后提示断电重启设备

- 新增 Qualcomm 9008 串口（COM）传输，兼容 QDLoader 串口驱动绑定

## v0.3.0

- 官方工具改为独立窗口启动，不再内嵌
- 修复免费声明期间原生子窗口覆盖/阻塞界面的问题
- 默认窗口化并修复旧的 1920x1080 全屏尺寸缓存
- Release 构建改为 Windows GUI 子系统，无控制台窗口
- 页面异步加载并缓存工具列表，降低切页延迟
- 新增 GitHub 最新版本自动检查；查询失败提供 Release 入口
- 清理并禁止打包私有 bug 扫描器/报告

## v0.21

- 新增 4 个文件解析工具：Qualcomm MBN/ELF、Oplus Digest、MTK Legacy PMT/MPT、Samsung PIT
- 新增 Samsung 工具分类
- 修复刷机台预检弹窗“继续执行”按钮引用未定义 `fastbootDevices` 导致的 QML TypeError
- 工具总数 220，全部可运行

# 更新记录

## v0.2

* 修复分区表复选框无法勾选：按行唯一 id 切换，支持表头全选。
* 新增「执行前检测」：写入/擦除前检查连接、型号、序列号、工作模式、USB 端口、驱动、分区、已选分区、镜像、引导文件与权限。
* 展锐 FDL1 → FDL2 切换增加明确日志（EXEC_DATA / 重新枚举 / FDL2 通道建立）。
* 瑞芯微 RKDevTool 模式页补齐 RockUSB 读写说明与入口文案。

## v0.1.1

* 修复 QML 运行时问题：ConsolePage 空设备 combo TypeError、PacketsPage Column 锚点警告、
  GuideFilePanel 未定义 `Theme.r8`、缺失图标（stethoscope / package-open 等 8 个）。
* 修复幽灵点击区域：列表行仅 hover 的 MouseArea 不再吞掉左键；任务卡取消按钮热区放大并加 hover 反馈。
* Fastboot 分区读取复用同一次 `getvar all` 结果，避免重复查询慢速 bootloader。
* 设置页「检查更新」改为打开真实 GitHub Release 页面。
* 最小窗口（1120×720）下刷机中心底部不再被状态栏遮挡。
* 全量审计：19 个界面视图 0 QML 警告、最近日志 0 error/warning、216 工具注册表一致。

## v0.1.0（分阶段开发汇总）

### 阶段 1：框架与界面
* C++20 + Qt 6.8 工程、工具注册表、任务引擎、日志、设置、原生窗口效果。
* 桌面工具风格界面（菜单栏/工具栏/侧栏/状态栏 + 紧凑表格），浅色/深色两套中性主题。
* 单实例运行、退出保护、任务面板、日志导出、诊断报告、环境自检。

### 阶段 1.5：命令行控制台
* 新增「控制台」页面：ADB shell / Fastboot 命令直接执行、输出实时显示、常用命令快捷按钮。
* 新增 `--adb-cmd` / `--fastboot-cmd` 无界面命令入口。

### 阶段 1.6：免责声明与安装包
* 首次启动强制展示免责声明，勾选同意后方可使用，状态持久化；支持 `--accept-disclaimer` / `--preview-disclaimer`。
* 三个压缩包正名为高通 / 联发科 / 紫光展锐深度刷机引导包（DA / Loader / FDL）。
* 新增 Inno Setup 安装包，可选桌面快捷方式，支持标准卸载。

### 阶段 1.7：刷机中心模式拆分
* Fastboot / Recovery / 9008(EDL) 在刷机中心按平台独立入口，不再混为「高通刷机」一类。
* 新增 Fastboot 通用工作台、Recovery 卡刷工作台、Qualcomm 9008 深度刷机工作台。
* 新增 `adb.sideload`（Recovery 卡刷）与 `adb.waitDevice`（等待设备上线）工具。
* 内置模式页同步增加 Fastboot / Recovery / Qualcomm 9008 三个入口。

### 阶段 1.8：自动识别模式与本地/引导包引导文件
* 刷机中心自动识别设备模式并切换到对应工作台，可关闭自动切换后手动选择。
* 深度刷机支持本地选择引导文件，或从三个引导包按机型提取免授权 Loader/DA/FDL，并自动填入工具参数。

### 阶段 2：镜像与固件工具链
* payload 信息/解包/打包/增量结构分析，boot/init_boot/vendor_boot 解包与重打包，
  cpio、ramdisk 修改、super 信息/拆分/合并/扩缩容、sparse、vbmeta、GPT、ELF、
  zip 卡刷包、压缩格式转换、zstd 解压、开机 LOGO 提取与打包、哈希/偏移计算/空白块扫描等。

### 阶段 3：设备与协议
* 高通 Sahara/Firehose、MTK BROM/DA/preloader/scatter、展锐 FDL/U2S/PAC、
  瑞芯微 RockUSB、全志 FEL；ADB/Fastboot 全套工具（含 bugreport/dmesg/应用冻结卸载/APK 导出/硬件信息报告）。
* 端口枚举与刷机端口过滤、驱动检测与安装、批量任务并发调度。

### 阶段 4：引导包与固件资源
* GeekFlashPacket 解析、机型/引导文件解析（MTK DA1/DA2、QC Loader、SPRD FDL1/FDL2、Preloader）。
* 离线机型库与官方固件入口；小米固件检索/下载/OTA、文件识别/解包/校验/分包合并。
* RKFW/RKAF、IMAGEWTY（PhoenixSuit）固件容器解析与解包。

### 阶段 5：内置模式与内嵌官方工具
* MiFlash / SP Flash / SPD Flash / RKDevTool / PhoenixSuit 五个工作台。
* MiFlash 真实 Fastboot 刷写后端（tgz 解包 → 顺序刷写 → 清数据/锁 BL/重启，低电量拒绝）。
* MiFlash、SP Flash Tool、Research Download 子窗口内嵌运行（不弹独立窗口）。

### 阶段 6：真机验证与安全加固
* 真机 `d251427e`（uke）：Fastboot 92 分区读取、210 个变量备份、分区表 CSV 导出、OEM device-info、电量检测。
* 刷写前检查（分区存在/容量/SHA256/电量）、刷写三重拒绝、重启模式归一化、单实例、退出保护。
* 修复：fastboot getvar 输出走 stderr 导致的解析失败、FAILED 行被误当变量、
  18D1:D00D 被误判为 ADB、透明窗口导致内嵌工具白屏、重启模式显示标签被直接当参数。

### 已知边界
* APatch 实际修补需在 APatch 管理器中完成；本工具提供内核检查、版本识别与 kernel/ramdisk 备份。
* 移除解锁红字未实现（需机型专属 ABL 修改）；现提供只读检测与官方恢复说明。
* 加密 OZIP/OFP 容器需厂商授权解密；本工具只解包未加密的 zip 兼容包。
* 各平台写入类操作需要对应模式真机与固件，部分流程仍待真机验证。
