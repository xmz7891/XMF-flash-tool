# 设备层（src/device）说明

设备层负责「在 PC 侧看到设备、和 Android 设备对话、检查/安装驱动、批量执行任务」。
它**不依赖 UI**：所有耗时操作都是 `ToolSpec` + handler，经 `App.runTool` 进入
`TaskManager` 的线程池执行；UI 只消费注册表与两个可选模型。

负责人：device。只修改 `src/device/**`、`scripts/fetch_platform_tools.ps1`、
`docs/DEVICE_LAYER.md`、`data/platform-tools/`。

---

## 1. 文件划分

| 文件 | 职责 |
| --- | --- |
| `DeviceEnumerator.h/.cpp` | SetupAPI 枚举所有在位的 PnP 设备（`RawDevice`），解析 VID/PID、COM 号、服务名、设备状态；纯解析函数（`parseVidPid` / `parseComPort`）可单测。 |
| `Ports.h/.cpp` | 把 `RawDevice` 分类成端口：`qcom-edl` / `mtk-brom` / `mtk-preloader` / `sprd-fdl` / `rk` / `aw-fel` / `adb` / `fastboot` / `serial` / `mtp`；`enumeratePorts()` 一次枚举，`summarizePorts()` 输出中文摘要。 |
| `PortsModel.h/.cpp` | `PortsModel`（`QAbstractListModel`，QML 可绑定）+ `PortsMonitor`（`QAbstractNativeEventFilter` 监听 `WM_DEVICECHANGE`，400ms 防抖后刷新）。支持选择/锁定端口。 |
| `PlatformTools.h/.cpp` | 定位随附的 `adb.exe` / `fastboot.exe`，统一执行与中文错误文案。 |
| `AdbClient.h/.cpp` | adb 封装：devices 解析、shell/exec-out、push/pull、reboot（含 edl/brom）、无线调试、forward、getprop、分区表、logcat/dmesg/bugreport、设备信息导出。 |
| `FastbootClient.h/.cpp` | fastboot 封装：devices 解析、getvar/getvar all、分区表、flash/erase/format、reboot、`flashing unlock/lock`、`oem`、信息导出。 |
| `DriverManager.h/.cpp` | 六类驱动（QDLoader/MTK/SPRD/RK/AW/ADB）绑定状态检测，`pnputil` 安装 INF，中文状态摘要。 |
| `BatchJobManager.h/.cpp` | 批量队列：每设备串行、多设备并行限流、进度聚合、取消、失败重试；附哈希清单解析 / 重命名模板 / 文件哈希校验等纯函数。 |
| `DeviceRegister.cpp` | 注册 `adb.*`、`fastboot.*`、`device.*`、`batch.*` 工具（全部 handler 在这里）。 |
| `tests/test_device.cpp` | 解析、端口/驱动识别、批处理调度、工具注册的单元测试。 |

分类与注册：端口扫描/驱动工具归入 `bootloader` 分类；ADB/Fastboot 归入 `adb`；
批量工具归入 `batch`。均为 `ToolRegistry` 数据驱动，无需改 QML。

---

## 2. platform-tools（adb / fastboot）

Android 官方 platform-tools 随附在 `data/platform-tools/`（构建时 `data/` 会整体复制到
`XMF.exe` 同级）。需要的文件：`adb.exe`、`fastboot.exe`、`AdbWinApi.dll`、
`AdbWinUsbApi.dll`、`libwinpthread-1.dll`。

下载/更新：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch_platform_tools.ps1
# 强制重新下载
powershell -ExecutionPolicy Bypass -File scripts\fetch_platform_tools.ps1 -Force
```

来源：`https://dl.google.com/android/repository/platform-tools-latest-windows.zip`
（脚本优先用 `curl.exe`，否则 `Invoke-WebRequest`；zip 缓存在 `%TEMP%\xmf-platform-tools`）。

**程序内查找顺序**（`locateTool()`）：

1. 环境变量 `XMF_PLATFORM_TOOLS`（指向包含 exe 的目录，便于测试/绿色版覆盖）
2. `<exe 同级>/data/platform-tools`
3. `<exe 同级>/resources/bin/platform-tools`
4. 系统 `PATH`

找不到时工具以中文报错并提示上述路径与 `fetch_platform_tools.ps1`。

---

## 3. 端口扫描与识别

`enumeratePorts()` 用一次 `SetupDiGetClassDevsW(DIGCF_PRESENT | DIGCF_ALLCLASSES)` 枚举
当前所有 PnP 设备，读取 `SPDRP_DEVICEDESC / FRIENDLYNAME / HARDWAREID / COMPATIBLEIDS /
SERVICE / CLASS / CLASSGUID / DRIVER / MFG / LOCATION_INFORMATION / BUSNUMBER / ADDRESS`，
并额外读取设备注册表 `PortName`（COM 号）与 `CM_Get_DevNode_Status`（问题代码，28 = 驱动未安装）。

识别依据（优先级从高到低）：

1. 描述里的 `ADB Interface` / `Android Composite ADB` / 接口类 `Class_FF&SubClass_42&Prot_01` → ADB
2. `Android Bootloader Interface` / `Fastboot` → Fastboot
3. 已知 VID/PID 表 → EDL/BROM/Preloader/FDL/Loader/FEL/Fastboot
4. 描述/服务关键词（`QDLoader`、`MediaTek`、`Spreadtrum/Unisoc`、`Rockchip`、`Allwinner`）
5. COM 号 → 普通串口；`MTP/Media Transfer` → Android 媒体设备

`PortsModel` 角色：`portName`、`comPort`、`description`、`kind`、`kindLabel`、`chipHint`、
`chipLabel`、`vid`、`pid`、`vidPid`、`service`、`instanceId`、`isFlashing`、`isAdb`、
`isFastboot`、`hasProblem`、`problemCode`、`data`。`refresh()` 同步枚举（毫秒级）；
`autoRefresh` 开启时由 `WM_DEVICECHANGE` 自动刷新；`lockPort(name)` 可在刷新后保持选中。

> 集成说明：`PortsModel` / `PortsMonitor` 目前未被 `AppController` 持有。若要暴露给 QML，
> 由 Lead 在 `AppController` 中创建 `gf::device::PortsModel` 并以 `Q_PROPERTY`/`qmlRegisterType`
> 注册即可（本模块不修改 `src/app`）。测试不依赖 QML。

---

## 4. ADB / Fastboot 封装

所有调用都通过 `util::runProcess()`（`QProcess`）执行，返回统一结构：

```cpp
struct AdbResult      { bool ok; int exitCode; QString output; QString error; };
struct FastbootResult { bool ok; int exitCode; QString output; QString error; };
```

约定：

* 输出合并 stdout + stderr（fastboot 的 `getvar` 结果在 stderr 上）。
* `error` 已含中文说明；工具 handler 用 `throw std::runtime_error(...)` 结束任务。
* 序列号为空时自动选择唯一在线设备；0 台/多台都给出中文错误（要求填写序列号）。
* 无设备时的标准文案：
  * `未检测到 ADB 设备，请开启 USB 调试并授权本机（设备管理器中应出现 ADB 接口）。`
  * `未检测到 Fastboot 设备，请将设备进入 Bootloader / Fastboot 模式后重试。`

解析函数（均可单测）：

* `parseAdbDevices()` — 兼容 daemon banner、`List of devices attached` 表头、
  `unauthorized` / `offline` / `no permissions`、`product:… model:…` 扩展列、TCP 序列号。
* `parseGetpropOutput()` — `[key]: [value]`。
* `parseByNameLinks()` — `ls -l /dev/block/by-name` 符号链接与裸 `ls` 两种格式。
* `parseFastbootDevices()` — `fastboot` / `fastbootd` 两列，容忍 `\r\n` 与错误输出。
* `parseGetvarOutput()` — `(bootloader) key:value`，保留 `partition-size:boot`、
  `has-slot:boot`、`is-logical:super`、`slot-successful:_a` 等嵌套键。
* `parsePartitionTable()` — 由 `getvar all` 生成按名称排序的分区表。

---

## 5. 驱动检测与安装

`detectDrivers()` 返回六类 `DriverStatus`（QDLoader / MTK / SPRD / RK / AW / ADB）：

* `state = Bound`：找到匹配设备且驱动服务已加载；
* `state = Missing`：找到匹配设备节点，但无服务或问题代码 28（驱动未安装）；
* `state = Absent`：没有找到匹配设备（未连接或驱动从未安装）。

`driverSummary()` 生成中文摘要（含是否管理员）。检测本身只读，不需要管理员。

安装：`installDriverInf(inf)` → `pnputil.exe /add-driver <inf> /install`，
退出码 0 / 3010 视为成功。**需要管理员**：非管理员直接返回中文错误，
提示「右键以管理员身份重新运行」。驱动包默认放 `data/drivers/`（递归查找 `*.inf`），
可用 `device.driver.install` 工具指定其它目录。失败时返回 pnputil 输出尾部。

---

## 6. 批量任务（BatchJobManager）

```cpp
BatchJobManager manager(/*parallel=*/2);
BatchJob job;
job.id = "flash-boot";
job.device = "SERIAL";            // 同 device 的任务串行执行
job.label = "刷写 boot";
job.maxAttempts = 2;              // 失败重试
job.run = [](BatchJobContext& ctx) {
    ctx.setProgress(30);
    ctx.checkCanceled();          // 取消时抛 BatchJobContext::BatchJobCanceled
    ctx.log("步骤 1 完成");
    return true;                  // false = 本次尝试失败
};
manager.addJob(job);
manager.setProgressCallback([&](int percent, const QString& status) { ... });
manager.setLogCallback([&](const QString& msg, bool isError) { ... });
BatchSummary summary = manager.run();   // 阻塞，直到全部完成/取消
```

调度语义：

* 全局最多 `parallelLimit` 个任务同时运行（默认 2，1..16）；
* 同一个 `device` 的任务绝不并发（设备级串行）；
* 任务返回 `false` 且未达到 `maxAttempts` 会重新排队重试；
* 进度 = 已完成任务数 / 总任务数（聚合百分比），终态回调 100；
* `requestCancel()` 线程安全：未启动的任务直接标记取消，运行中的任务通过
  `BatchJobContext::isCanceled()` 感知；`run()` 结束后队列清空，可复用管理器；
* `run()` 在工作线程中调用（`App.runTool` 的 handler 内），内部用 `std::thread` 执行
  job，因此不阻塞 GUI。

批量工具：

* `batch.queue` — 对多台设备执行同一条 ADB 命令（并发 + 重试 + 取消）；
* `batch.script` — 每行一条命令，逐台设备顺序执行；
* `batch.verify` — 按 `.sha256` / `.md5` 清单批量校验固件（支持 sha256sum 与
  `SHA256 (file) = hash` 两种清单格式）；
* `batch.rename` — 按模板批量重命名（默认仅预览），模板支持
  `{name} {index} {index2} {index3} {ext} {filename}`。

「二维码式批处理」属于后续 UI 流程，本层只提供与 UI 无关的队列/限流/聚合/取消/重试原语。

---

## 7. 注册的工具

| 分类 | 工具 id |
| --- | --- |
| `adb` | `adb.devices`、`adb.server`、`adb.shell`、`adb.reboot`、`adb.info`、`adb.partitions`、`adb.logs`、`adb.wireless`、`adb.forward`、`adb.push`、`adb.pull`、`adb.install` |
| `adb`（fastboot 组） | `fastboot.devices`、`fastboot.getvar`、`fastboot.partitions`、`fastboot.flash`、`fastboot.erase`、`fastboot.reboot`、`fastboot.lock`、`fastboot.info`、`fastboot.oem` |
| `bootloader` | `device.ports`、`device.drivers`、`device.driver.install` |
| `batch` | `batch.queue`、`batch.script`、`batch.verify`、`batch.rename` |

危险操作（`fastboot erase`、`fastboot flashing lock/unlock`、`fastboot flash`）都有
必填确认勾选或明确的分区/镜像参数校验；无设备、无 platform-tools、无管理员权限时
一律返回中文错误而不是崩溃。

---

## 8. 构建与测试

```powershell
scripts\build.cmd -BuildType Debug -BuildDir build\device
scripts\build.cmd -BuildType Debug -BuildDir build\device -Target xmf_tests
build\device\bin\xmf_tests.exe
```

单元测试（`src/device/tests/test_device.cpp`）覆盖：

* `adb devices -l`（在线/离线/未授权/no permissions/daemon banner/TCP 序列号）；
* `getprop`、`/dev/block/by-name`、`fastboot devices`、`getvar all`、分区表解析；
* VID/PID、COM 号、六类刷机端口与六类驱动匹配；
* 哈希清单解析、重命名模板、文件哈希校验；
* 批处理队列：全量执行、并发上限、同设备串行、失败重试、运行前/运行中取消、
  进度聚合、失败汇总；
* 工具注册完整性（id / handler / category 合法性）。


## Fastboot 设备读取

`AppController` 会在没有 ADB 设备时回退到 `fastboot devices`，状态栏显示
“设备: <serial> [Fastboot]”。`FirmwareWorkspace::loadFastbootPartitions()` 解析
`fastboot getvar all`（fastboot 把变量写在 stderr，解析时 stdout/stderr 合并），
把 `partition-size:<name>` / `partition-type:<name>` 转换成刷机中心的分区表行。
该路径只读，不会写设备；`getvar` 在引导器忙时可能超时，界面会给出错误提示。


## 真机验证记录

2026-11-09，真机 `d251427e`（fastboot 模式，product=uke，current-slot=a，unlocked=no）：

* `fastboot.partitions`：读取到 **92 个分区**（ALIGN_TO_128K、abl_a/b、aop、apdp、bk51…），
  刷机中心分区表与状态栏（设备 uke / BL 未解锁 / 槽位 a）均正确显示；
* `bootloader.backup.env`：读取 **210 个 getvar 变量 + 92 个分区**，写出 9KB 只读快照；
* `fastboot.info`：导出 fastboot 信息文本；
* 之前出现的 `AdbWriteEndpointSync failed (121)` 经物理拔插后恢复，证实为设备端点挂死而非软件问题。

* `fastboot.oem device-info`：Verity true / Device unlocked false / critical unlocked true；
* `fastboot getvar all`：423 行输出正常解析；
* 2026-11-09 尝试 `fastboot reboot` 回到系统时，设备电池仅 **3456 mV 且 battery-soc-ok=no**，
  重启后设备回到 Bootloader（疑似低电量保护）。据此新增安全策略：
  * Fastboot 重启工具会提示电量异常；
  * MiFlash 刷写前会检查电池状态，异常时直接拒绝刷写，避免中途断电变砖。

## 安全校验

* `fastboot.flash.check`：刷写前只读检查目标分区是否存在、分区容量、镜像大小与占比、SHA256、电量；
  真机 `boot_a`（96.0 MB）验证通过，拼错分区名会直接报"设备上找不到分区"。
* `fastboot.flash`：电量异常、分区不存在、镜像超过分区容量三种情况都会在写入前拒绝。
* `fastboot.reboot`：电量异常时给出警告；重启模式支持中英文显示标签归一化（system/bootloader/
  recovery/fastbootd/edl/poweroff），已用真机验证命令参数正确。
