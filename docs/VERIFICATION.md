# 验证记录

版本：XMF Flash Tool v0.1.0
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
