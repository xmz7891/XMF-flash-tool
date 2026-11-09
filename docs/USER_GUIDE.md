# XMF Flash Tool 使用指南

## 1. 安装与启动

* **安装方式一（推荐）**：运行 `XMF-Flash-Tool-Setup-0.1.0.exe`，按向导安装（默认当前用户目录，无需管理员权限），
  可选创建桌面快捷方式，卸载可在「设置 → 应用」或安装目录的卸载程序完成。
* **安装方式二（便携）**：下载 `XMF-Flash-Tool-v0.1.0-full.zip`，解压到任意目录（不要放在中文/只读路径下），双击 `XMF.exe`。
* **首次启动会显示免责声明**：必须勾选「我已阅读并同意」并点击「同意并继续」后才能进入主界面；
  同意状态会保存，之后启动不再重复弹出。点击「退出」则直接关闭程序。
* 驱动安装、官方工具内嵌、刷写操作建议以管理员身份运行。
* 单实例：重复双击只会把已打开的窗口激活，不会启动第二个进程。
* 首次启动可在「设置 → 运行自检」确认内置工具、引导包与驱动是否完整。

## 2. 界面速览

* **顶部**：菜单栏（文件 / 设备 / 工具 / 视图 / 帮助）+ 快捷工具栏 + 端口/设备计数。
* **左侧**：概览、刷机中心、工具库、内置模式、深度刷机引导包、固件资源、批量任务、日志、官方工具、设置、命令控制台。
* **状态栏**：设备、BL 状态、槽位、端口、驱动、电量（Fastboot 模式）与版本。
* **右下角任务面板**：实时进度、日志、取消按钮；有任务运行时关闭窗口会先要求确认。

## 3. 常用流程

### 3.1 刷机中心：先选模式，再刷机
Fastboot / Recovery / 9008(EDL) 是三种完全不同的模式，刷机中心按平台分别提供入口：

| 模式 | 适用场景 | 协议 / 驱动 |
| --- | --- | --- |
| Fastboot | 已解锁 BL，刷 boot / recovery / vbmeta 等分区 | bootloader + fastboot |
| Recovery | 卡刷 OTA / 第三方 zip | Recovery + ADB Sideload |
| 9008 EDL | 高通深度刷机 / 救砖 | Sahara + Firehose（需要 programmer） |

1. 「刷机中心」会**自动识别设备当前模式**（ADB / Fastboot / Recovery Sideload / 9008 EDL / BROM / FDL /
   MaskROM / FEL），并自动打开对应工作台；识别条上可关闭「自动切换到对应刷机模式」。
   若没有自动切换，直接在下方 5 个平台卡片里**手动选择**即可。
2. 「刷机中心」顶部选择平台（高通 / 联发科 / 紫光展锐 / 瑞芯微 / 全志），再点对应模式。
2. Fastboot 模式：点「读取分区」查看分区表，先「刷写前检查」再「刷写分区」。
3. Recovery 模式：先用「重启到 Recovery」或手动进入 Recovery，开启 ADB Sideload 后选择 zip 卡刷。
4. 9008 模式：页面顶部「深度刷机引导文件（免授权）」可**选择本地 Loader/programmer**，或从
   高通 / 联发科 / 紫光展锐引导包中按机型提取 Loader/DA/FDL（自动填入当前引导文件），
   然后按步骤执行 Sahara 握手 → 上传 Firehose programmer → Firehose 存储信息 → 读写/擦除扇区。
   其它深度模式也可以在「深度刷机引导」页点「引导文件」做同样的本地选择 / 引导包提取。
5. ADB 设备可在「刷机中心 → 读取设备分区」读取完整分区表；分区表可导出 CSV。

### 3.2 刷入官方固件（MiFlash 线刷包）
1. 「内置模式 → MiFlash」。
2. 选择小米/红米 `.tgz` 线刷包，镜像列表会列出包内分区。
3. 选择模式：全部删除 / 保留用户数据 / 全部删除并锁 BL。
4. 点「刷机」。刷写前会强制检查电量、分区容量与镜像大小（电量异常直接拒绝）。

### 3.3 刷写单个分区
1. 工具库 → `fastboot.flash.check` 先做只读检查（分区存在、容量、SHA256、电量）。
2. 检查通过后再用 `fastboot.flash` 刷写。

### 3.4 批量任务
1. 「批量任务」选择带文件参数的工具（支持单文件与多文件参数工具）。
2. 添加文件/文件夹，设置并发数（1–4），点「开始批量」。
3. 也可用命令行自动化：
   `XMF.exe --batch-tool image.convert.detect --batch-field input --batch-files "a.bin;b.bin" --batch-parallel 2`

### 3.5 深度刷机引导包（高通 / 联发科 / 紫光展锐）
* 三个平台引导包位于 `data/packets`，启动时自动扫描：高通 EDL、联发科 EDL、紫光展锐 U2S。
* 「深度刷机引导」页可选择平台引导包查看品牌/系列/机型；「解析引导文件」按机型解析
  Qualcomm Loader / MTK DA1/DA2 / SPD FDL1/FDL2 等深度刷机引导文件。
* MTK/QC/SPRD 刷写流水线会自动按机型从对应平台引导包取引导文件。

## 4. 安全须知

* **电量**：刷写前请确保电量充足；MiFlash 刷写会检查 `battery-soc-ok` 与电压，低于约 3.6V 会拒绝。
* **备份**：擦除 / 刷写 userdata、metadata 等操作不可逆；重要数据先备份。
* **分区名**：刷写工具会校验分区是否存在、镜像是否超过分区容量，拼错分区名会直接拒绝。
* **来源**：不要刷入来源不明的参数/ABL 文件；本工具不会绕过厂商签名与账号验证。

## 5. 内置官方工具

「官方工具」页可内嵌运行 MiFlash、SP Flash Tool、Unisoc Research Download（不弹独立窗口），
也可「独立窗口」打开或「重新启动」。窗口尺寸会自动适配主界面内容区。

## 6. 命令行

```powershell
XMF.exe --run-tool system.selfTest          # 环境自检（19 项）
XMF.exe --diagnostics                       # 导出诊断报告到日志目录
XMF.exe --dump-tools                        # 输出全部工具 JSON
XMF.exe --batch-tool <id> --batch-field <key> --batch-files "a;b" --batch-parallel 2
XMF.exe --run-tool fastboot.partitions      # 读取已连接设备分区
XMF.exe --auto-read                         # 打开刷机中心并自动读取设备分区
XMF.exe --adb-cmd "getprop ro.product.model"  # 直接执行一条 ADB shell 命令
XMF.exe --fastboot-cmd "getvar all"           # 直接执行一条 fastboot 命令
XMF.exe --theme dark --page 1 --screenshot out.png --screenshot-delay 2000
```

## 7. 常见问题

* **看不到设备**：确认数据线支持数据传输；安装驱动（「官方工具 → 安装驱动」或「概览 → 安装驱动」）；高通需进入 9008，MTK 需 BROM/Preloader，SPRD 需 FDL，RK 需 MaskROM/Loader，全志需 FEL。
* **Fastboot 报 `error 121 / AdbWriteEndpointSync failed`**：设备端点挂死，拔插数据线即可恢复（已实测）。
* **Fastboot 电量显示红色**：电池状态异常或电压偏低，请先充电。
* **内嵌官方工具白屏**：确认使用本工具内嵌启动而不是系统缩放异常；如仍异常可用「独立窗口」。
* **日志**：「日志」页可筛选/搜索/复制/导出；「帮助 → 导出诊断信息」会生成系统与设备状态报告。
