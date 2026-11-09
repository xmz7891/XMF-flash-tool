# 验证记录

版本：XMF Flash Tool v0.1.2
环境：Windows 11 · Qt 6.8.3 · MinGW 13.1 · CMake 3.30 · C++20

## 1. 构建

| 配置 | 结果 |
| --- | --- |
| Debug（`build/Debug`） | 编译通过，无错误 |
| Release（`build/Release`，`GF_BUILD_TESTS=OFF`） | 编译通过，windeployqt 打包 |
| 便携包 | `dist/XMF-Flash-Tool`（lite）、`dist/XMF-Flash-Tool-full`（full） |

## 2. 单元测试

```
build/Debug/bin/xmf_tests.exe
42 test class(es), 0 failed
```

覆盖模块：core（工具注册、任务、设置）、image（51+ 镜像工具与解析器）、rk（RKFW/RKAF）、
aw（IMAGEWTY）、packets（GeekFlashPacket 解析）、device（ADB/Fastboot 解析、批量任务）、
extras（LZ4、boot 修补、root 检测、APatch 内核检查、bootloader、旁路信息）、flows、app 等。

## 3. 随包校验脚本

```
powershell -ExecutionPolicy Bypass -File scripts/verify_package.ps1
PASS: 9 checks OK
```

检查项：`XMF.exe`、`data/packets`、`data/platform-tools`、`data/drivers`、
`data/thirdparty`、`zstd.exe`、`docs/USER_GUIDE.md`、`docs/FEATURE_COVERAGE.md`、
内置环境自检（19 项：目录、工具注册、adb/fastboot/zstd、引导包 DA/Loader、驱动 inf、
端口、gzip 往返等，0 警告 0 失败）。

## 3.5 首次启动免责声明

* 首次启动（或重置设置后）显示模态免责声明，背景内容全部被遮罩且不可交互；
* 未勾选「我已阅读并同意」时「同意并继续」按钮禁用；勾选后点击写入设置并进入主界面；
* 点击「退出」直接关闭程序；
* 已同意状态持久化（QSettings），后续启动不再弹出；
* 界面截图验证：`build/disclaimer2.png`、安装后版本 `build/installed_disclaimer.png`；
* 无界面模式（`--run-tool` / `--batch-tool` / `--screenshot` 等）自动跳过，不污染自动化；
  `--accept-disclaimer` 可在部署时静默同意，`--preview-disclaimer` 可强制预览。

## 3.6 Inno Setup 安装包

* 安装包：`dist/installer/XMF-Flash-Tool-Setup-0.1.0.exe`（Inno Setup 6，默认按当前用户安装，可选桌面快捷方式）；
* 静默安装到临时目录：退出码 0，安装后 `XMF.exe`、`unins000.exe` 均存在；
* 安装后运行 `--run-tool system.selfTest`：19 项全过，`--dump-tools` 显示 214/214 工具；
* 安装后 `--preview-disclaimer` 截图正常；
* 静默卸载：退出码 0，安装目录被完整删除。

## 3.7 刷机中心模式拆分

* 刷机中心按 5 个平台分别列出 Fastboot / Recovery / 深度刷机三种入口；
* 新增工作台截图验证：`build/fb3.png`（Fastboot）、`build/rec3.png`（Recovery）、`build/edl5.png`（9008 EDL）、
  `build/flashcenter_new2.png`（刷机中心总览）、`build/modes_new.png`（内置模式）；
* `--flash-mode fastboot:qcom|recovery:qcom|qcom-edl` 可用于自动打开对应工作台做截图/验收。

## 3.8 自动识别模式与引导文件

* 插入 Fastboot 设备后打开刷机中心：自动识别为「Fastboot 模式」并自动打开 Fastboot 工作台
  （截图 `build/fc_auto_final.png`）；`--no-auto-switch` 可关闭自动切换（截图 `build/fc_noauto_final.png`）。
* 自动识别覆盖 ADB / Fastboot / Sideload / 9008 / BROM / FDL / MaskROM / FEL，依据 ADB 状态、fastboot 设备与 USB 端口分类。
* 9008 工作台顶部新增引导文件选择：本地文件 / 引导包（按机型 + Loader/DA/FDL 属性提取）。
  实测高通引导包 `439全系` 的 `Loader` 成功解析为 `XiaomiCommon439_devprg`（448.8 KB）。

## 4. UI 冒烟测试

```
powershell -ExecutionPolicy Bypass -File scripts/smoke_test.ps1
PASS: 12 pages rendered
```

页面：概览、刷机中心、工具库、内置模式、深度刷机引导包、固件资源、批量任务、日志、
官方工具、设置、控制台、MiFlash 工作台。每页截图 90–260KB，无 QML 报错。

## 4.5 工具注册表

```
XMF.exe --dump-tools
tools=216 runnable=216 planned=0
```

13 个分类：镜像/固件、高通 9008、联发科、Bootloader、展锐、瑞芯微、全志、
Root/KSU/Magisk、账号锁/清除、ADB/Fastboot、固件资源、批量/自动化、机型旁路/工程。
完整清单见 `docs/TOOL_LIST.md`。

## 6. 深度刷机引导包（真机/离线验证）

| 引导包 | 识别 | 机型 | 品牌 | 引导文件 |
| --- | --- | --- | --- | --- |
| `package_mtk_edl-master.zip` | MTK | 80 | 6 | DA1/DA2（MT6895_DA1.bin） |
| `package_qualcomm_edl-master.(1).zip` | QC | 473 | 23 | Loader（XiaomiCommon439_devprg） |
| `package_sprd_u2s-master.zip` | SPRD | 1 | 1 | FDL1/FDL2 |

合计 554 个机型；扫描/导出清单/按机型解析引导文件均已实测。

## 7. 真机验证（`d251427e` / product=uke）

设备状态：Fastboot、`current-slot=a`、`unlocked=no`、`secure=yes`。

| 项目 | 结果 |
| --- | --- |
| `fastboot devices` / 端口识别 | 通过（VID_18D1 PID_D00D，Fastboot） |
| 分区读取 `fastboot.partitions` | **92 个分区**（ALIGN_TO_128K、abl_a/b、aop、apdp、bk51 …） |
| 分区表 CSV 导出 | 93 行（含表头），名称/大小/类型/槽位正确 |
| `getvar all` 备份 | **210 个变量 + 92 个分区**，9KB 快照 |
| OEM device-info | Verity true / Device unlocked false / critical unlocked true |
| 状态栏 | 设备 uke、BL 未解锁、槽位 a、电量显示 |
| 刷写前检查 | boot_a 96.0MB、镜像 1MB（1%）、SHA256、低电量告警 |
| 错误分区拒绝 | `no_such_part` → 明确报错，不写入 |
| 电池检测 | 电压/电量状态/充电屏读取正常，低电量红色告警 |
| 重启模式归一化 | `system — 重启到系统` → 正确发送 `reboot` |
| 写入类操作 | **未执行**：设备电池电压约 630mV（远低于锂电池下限），
厂商保护导致重启回到 Bootloader；为避免变砖，按安全策略拒绝所有刷写/擦除。 |
| ADB 工具 | **未验证**：设备无法启动到系统且电量不足，ADB 不可用。 |

## 8. 已知边界

* MTK seccfg/LK 等硬操作、加密 OZIP/OFP 需要厂商专属算法与授权，未实现。
* 移除开机红字需要机型专属 ABL/参数分区修改，仅提供检测与官方恢复说明。
* APatch 实际内核修补需在 APatch 管理器完成，本工具只做检查与备份。
* 不绕过厂商签名、账号验证或防回滚；相关"清除/解锁"工具只提供官方可行路径。
* 各平台写入类流程需要对应模式真机与固件，待设备电量恢复后补充验证。

## 9. 复现命令

```powershell
# 构建
Set-ExecutionPolicy -Scope Process Bypass -Force; . .\scripts\env.ps1
& $env:XMF_CMAKE --build build\Debug --target XMF
& $env:XMF_CMAKE --build build\Debug --target xmf_tests
.\build\Debug\bin\xmf_tests.exe

# 打包 + 校验
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package.ps1 -BuildDir build\Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_package.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\smoke_test.ps1
```


## 10. 全项目审计（v0.1.1）

对 19 个界面视图（11 个主页面 + 5 个内置模式 + Fastboot/Recovery/9008 工作台）做运行时审计：

| 项目 | 结果 |
| --- | --- |
| QML 运行时警告 / TypeError | 0（修复了 ConsolePage 空设备 combo、PacketsPage Column 锚点、GuideFilePanel 未定义 radius、缺失图标等） |
| 应用日志 `[error]` / `[warning]` | 最近 40 个日志文件 0 条（正常使用时） |
| 幽灵点击区域 | 扫描全部 MouseArea，修掉 5 处只有 hover、却默认吞掉左键的列表行；任务卡取消按钮改为 20×20 可见热区 |
| 死按钮 / 假按钮 | 扫描全部 AppButton / MenuItem，无空实现；设置页「检查更新」改为打开真实 Release 页面 |
| 图标资源 | 扫描 C++ 工具 icon 与 QML IconImage 引用，补齐 8 个缺失 SVG |
| 工具注册表 | 216 个工具，id 唯一、无空名称/分类、全部 runnable |
| 单元测试 | 42 个测试类 0 失败 |
| 环境自检 | 19 项通过，0 警告 0 失败 |
| 最小窗口 | 1120×720 下刷机中心底部不再被状态栏遮挡；设置页可滚动 |


## 11. v0.1.2：单屏刷机台 UI 重构与文字溢出审计

### 界面重构

* 主窗口改为**单屏刷机台**：顶部命令菜单（功能 / 通用功能 / 高级功能 / 其他功能 / 辅助功能）+ 左侧图标轨 +
  分区表 / 实时日志 / 引导与固件 / 操作与进度同屏；其余页面以叠页打开，顶部「返回刷机台」。
* 默认窗口 **1180×760 居中**，最小 **1040×680**；正常关闭保存尺寸与位置，首次启动不再铺满屏幕。
* 浅色主题改为暖灰 + 琥珀色（C 方案），深色主题同步；分组头使用深色仪器条。
* 标题栏按钮重绘（最小化 / 最大化 / 关闭），关闭按钮按主题着色，修复右上角白叉残影。

### 文字溢出审计与修复

| 问题 | 处理 |
| --- | --- |
| 设置页说明文字压住右侧开关/按钮 | SettingsRow 改为严格两列：标签列限宽 + 省略，控件列固定起点；说明文案压缩 |
| 设置页分组标题提示被裁切 | GlassSurface 分组头标题 / 提示各自限宽 + 省略 |
| 工具库分类标签被窗口裁掉 | 分类标签改为 Flow 自动换行（1180 / 1040 下均完整显示） |
| 页面标题栏右侧按钮贴窗口边缘被裁 | PageHeader 操作区统一右侧 16px 内边距；标题、副标题自动省略 |
| 固件资源页底部官方入口说明压住列表 | 调整列表与底部区高度，说明完整显示 |
| 日志 / 官方工具 / 批量 / 模式 / 命令控制台 | 逐页截图检查 1180×760 与 1040×680，未再发现文字裁切或压控件 |

### 验证结果

| 项目 | 结果 |
| --- | --- |
| Debug 构建 | 通过 |
| Release 构建（GF_BUILD_TESTS=OFF） | 通过 |
| 单元测试 | **42 个测试类，0 失败** |
| 随包校验 `verify_package.ps1` | **PASS: 9 checks OK**（19 项环境自检通过） |
| UI 冒烟 `smoke_test.ps1` | **PASS: 15 pages rendered**（11 个入口 + 4 个工作台） |
| 最小窗口 | 1040×680 下刷机台 / 设置 / 固件 / 资源页面布局正常 |
| 首次启动 | 1180×760 居中，免责声明完整展示 |
| 安装包 | `XMF-Flash-Tool-Setup-0.1.2.exe` 构建通过（264.9 MB） |
| 截图 | README 10 张截图按新 UI 重新生成 |
