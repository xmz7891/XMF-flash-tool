# 扩展工具层（src/extras）说明

本模块提供 Root / Magisk / KernelSU、Bootloader、解锁清除、工程模式（机型旁路）四组工具。
所有工具都通过 `ToolRegistry` + `TaskContext` 注册，运行在任务线程池中：长任务报进度、
可取消、失败抛中文错误；无设备（ADB/Fastboot）时给出清晰中文提示，不会崩溃。

负责人：device。修改范围：`src/extras/**`、`docs/EXTRAS.md`。

---

## 1. 文件划分

| 文件 | 职责 |
| --- | --- |
| `ExtrasRegister.cpp` | `registerBatchTools()` 兼容入口（批量工具由设备层注册，这里保持幂等空实现）。 |
| `ExtrasUtils.h/.cpp` | 公共工具：选项解析、`adb`/`su` 封装、shell 引号转义、输出目录/日志。 |
| `Lz4Codec.h/.cpp` | 自实现 LZ4 block 编解码 + legacy/modern 帧解析与打包，用于 Android lz4 ramdisk。 |
| `RootImage.h/.cpp` | boot/init_boot 修补核心（纯函数）：cpio 解包、init 备份/替换、注入文件、属性修改、按原压缩格式重打包。 |
| `RootSupport.h/.cpp` | Root 状态判定（Magisk/KernelSU/APatch/su）、Magisk 模块列表/标志解析、模块操作。 |
| `RootTools.cpp` | `registerRootTools()` 与全部 Root 工具 handler。 |
| `BootloaderSupport.h/.cpp` | `fastboot getvar all` 解析：版本、槽位、unlock/secure/verity 状态（纯函数）。 |
| `BootloaderTools.cpp` | `registerBootloaderTools()` 与全部 Bootloader 工具 handler。 |
| `UnlockTools.cpp` | `registerUnlockTools()` 与擦除/清除类工具 handler。 |
| `BypassSupport.h/.cpp` | 工程模式拨号指令/Activity 表、机型出厂信息解析（纯函数）。 |
| `BypassTools.cpp` | `registerBypassTools()` 与工程模式/旁路信息工具 handler。 |
| `tests/test_extras.cpp` | LZ4、ramdisk/boot 修补、Root 判定、模块解析、Bootloader 解析、工程模式辅助、工具注册的单元测试。 |

---

## 2. Root / Magisk / KernelSU / APatch（category=root）

| 工具 id | 名称 | 说明 |
| --- | --- | --- |
| `root.status` | Root 状态检测 | 通过 `su -c id`、`magisk -v`、`/data/adb/ksu`、`/data/adb/apatch` 等判断是否 Root 及方案；同时读取 `ro.boot.flash.locked`、`ro.boot.verifiedbootstate`、构建类型。 |
| `root.boot.patch` | Boot 镜像修补 | 读取 boot/init_boot，解包 ramdisk（gzip/lzop/lz4/裸 cpio），把用户提供的 magiskinit / KernelSU 文件注入为 `/init`，原 init 备份到 `.backup/init`，写入 `.backup/.magisk` 配置，再按原格式重打包输出新镜像。 |
| `root.prop.patch` | ramdisk 属性修改 | 修改 ramdisk 内 `default.prop` / `prop.default` 的键值（例如 `ro.debuggable=1`），保留其它行并输出新 boot/init_boot。 |
| `root.modules` | 模块列表 | 读取 `/data/adb/modules`，显示模块 id/名称/版本与 `disable` / `remove` / `update` 标志。 |
| `root.module` | 模块管理 | 安装模块 zip（push 到 `/data/local/tmp` 后 `magisk --install-module`）、卸载（写 `remove` 标志）、禁用/启用（写/删 `disable` 标志），可选自动重启。 |
| `root.hide` | Root 隐藏 / 检测对抗 | **规划中**：隐藏 Root/模块/su 痕迹需要按机型与检测项适配，本版本未实现。 |
| `root.apatch.patch` | APatch 内核修补 | **规划中**：APatch 需要修改内核并处理 KPM，本版本未实现，请使用 APatch 官方管理器。 |

### boot 修补使用说明

1. 从**官方 Magisk APK** 中提取 `magiskinit`（APK 内的 `lib/<abi>/libmagiskinit.so`，重命名为
   `magiskinit`），或使用 KernelSU 提供的注入文件。
2. 从当前系统版本对应的固件包中取出 `boot.img`（Android 13+ 的 GKI 设备用 `init_boot.img`）。
3. 选择 `root.boot.patch`，填写镜像、注入文件与输出目录，勾选保留 dm-verity（按需）。
4. 用 Fastboot 刷入对应分区：`fastboot flash boot <修补后的镜像>`（init_boot 则刷 `init_boot`），
   然后重启。首次启动可能较慢。

修补算法（`RootImage.cpp`）：

* 解析 boot header，按 magic 自动识别 v0-v4 与 init_boot/vendor_boot；
* `boot` 与 `init_boot` ramdisk 支持 gzip / lzop / **lz4（legacy 与 modern 帧）** / 裸 cpio；
  `vendor_boot` 的分段 ramdisk 暂不支持（会明确报错）；
* cpio 条目全名兼容 `init` 与 `./init` 两种风格，修补后不会出现重复 init；
* 原 init 备份到 `.backup/init`，`magiskinit` 以 0750 权限替换 `/init`；
  `.backup/.magisk` 写入 `KEEPVERITY` / `KEEPFORCEENCRYPT` / `SHA1`；
* 重新压缩时保持原压缩格式（lz4 保留 legacy/modern 帧类型），再写回 boot header。

> 说明：本工具实现的是 Magisk「ramdisk init 替换」类修补，**不修改内核**。KernelSU 的非 GKI
> 机型需要内核补丁，本版本不实现；APatch 同理。请优先使用各自官方管理器。

---

## 3. Bootloader（category=bootloader）

| 工具 id | 名称 | 说明 |
| --- | --- | --- |
| `bootloader.info` | Bootloader 信息 | `getvar all` 读取 bootloader 版本、产品名、序列号、当前槽位、槽位数、unlock/secure/verity、A/B 槽位有效性与重试次数。 |
| `bootloader.getvar` | 读取 Fastboot 变量 | 读取单个变量或 `all`（只读）。 |
| `bootloader.slots` | A/B 槽位 | 查看槽位状态；可执行 `fastboot --set-active=a/b` 切换活动槽位（需确认）。 |
| `bootloader.setvar` | 写入 Bootloader 变量 | **白名单**：只允许 `current-slot`（底层 `--set-active`）。其它 bootloader 变量为只读，避免误改参数分区。 |
| `bootloader.unlock` | Bootloader 解锁 / 回锁 | `fastboot flashing unlock / lock`；明确提示会清除数据、部分机型回锁不可逆。 |
| `bootloader.verify` | 引导校验状态 | 读取 unlocked / secure / dm-verity / veritymode / verified boot 状态并给出风险提示。 |
| `bootloader.redtext` | 移除解锁红字提示 | **规划中**：红字由厂商参数分区/机型特性决定，需要按机型逆向适配，未实现。 |
| `bootloader.backup.env` | 参数备份/恢复 | **规划中**：需要机型专属分区布局，未实现。 |

安全说明：`bootloader.unlock` 仅限**本人所有**设备；解锁后设备数据会被清除，部分支付/银行类
应用会拒绝运行。不要在他人设备上执行解锁、回锁或参数修改。

---

## 4. 解锁 / 清除（category=unlock）

| 工具 id | 名称 | 说明 |
| --- | --- | --- |
| `unlock.userdata` | 格式化 userdata | `fastboot erase userdata` 或 `format userdata ext4`（更彻底），清除全部用户数据。 |
| `unlock.metadata` | 清除 metadata | `fastboot erase metadata`，用于解密失败、重刷系统后无法进入系统等场景。 |
| `unlock.cache` | 擦除 cache | `fastboot erase cache`，OTA 失败/启动异常时使用。 |
| `unlock.frp` | 擦除 FRP 分区 | `fastboot erase frp`，仅限本人设备恢复使用（工具描述与运行日志都会明确提示）。 |
| `unlock.dalvik` | 清除 dalvik-cache | 通过 root 的 `rm -rf /data/dalvik-cache/*`（需要 su）。 |
| `unlock.simlock` | SIM / 运营商状态 | **只读**读取 SIM 状态与运营商属性，不执行网络锁解锁。 |
| `unlock.account.official` | 账号锁官方移除流程 | **规划中**：只列出各厂商官方账号移除/激活锁解除流程（需本人凭证），不含绕过实现。 |
| `unlock.sim.official` | 运营商网络锁官方解锁 | **规划中**：解除 SIM 网络锁需要运营商官方解锁码，仅提供流程说明。 |

### 合规声明（重要）

本模块**不实现**任何绕过账号验证、FRP、运营商网络锁或 DRM 的漏洞利用：

* FRP 相关工具只做「本人设备的官方恢复流程」与擦除类命令，运行前必须明确确认设备归属；
* 账号锁 / 运营商锁只保留「官方流程说明」类规划项（`addPlanned`），不提供绕过实现；
* 不提供 IMEI 修改、基带伪造、付费解锁码生成等功能；
* Root / Bootloader 工具会显著降低设备安全性，仅用于本人设备的学习、备份与维修。

---

## 5. 工程模式 / 机型信息（category=bypass）

| 工具 id | 名称 | 说明 |
| --- | --- | --- |
| `bypass.codes` | 工程模式拨号指令 | 常见 `*#*#...#*#*` 入口列表（AOSP 测试菜单、小米 CIT、MTK 工程模式、Sony 服务菜单等），只读提示。 |
| `bypass.dial` | 打开工程模式拨号 | 通过 `adb shell am start -a android.intent.action.DIAL -d 'tel:...#...'` 尝试打开所选入口；失败时提示手动拨号。 |
| `bypass.activities` | 工程模式入口列表 | 常见工程/测试 Activity 组件列表（AOSP 测试设置、开发者选项、RadioInfo、MTK 工程模式、MIUI CIT 等）。 |
| `bypass.factory` | 工程模式启动 | 通过 `am start -n package/activity` 尝试启动所选入口；组件不存在时给出中文说明。 |
| `bypass.deviceinfo` | 机型 / 出厂信息 | 读取型号、厂商、设备代号、芯片平台、Bootloader、基带、序列号、Build 指纹、Android/SDK、安全补丁，可导出文本。 |
| `bypass.frp.guide` | FRP 官方恢复流程说明 | **规划中**：仅整理官方处理渠道与材料清单，不含绕过实现。 |
| `bypass.carrier.guide` | 运营商/厂商工程权限说明 | **规划中**：工程模式与网络锁权限由厂商/运营商控制，仅汇总官方申请流程。 |

拨号指令与 Activity 名称来自常见 ROM，**厂商可能改名或移除**；工具失败时只提示，不会修改设备。

---

## 6. 依赖

| 依赖 | 用途 |
| --- | --- |
| `gf_device::AdbClient` / `FastbootClient` | 所有 adb/fastboot 操作；platform-tools 查找顺序与设备层一致（`data/platform-tools` → `resources/bin/platform-tools` → PATH）。 |
| `gf_image`（`BootImage` / `Cpio` / `Compression`） | boot header 解析/序列化、cpio 归档、gzip/lzop 压缩。 |
| `src/extras/Lz4Codec` | LZ4 block 编解码 + legacy/modern 帧（不依赖 zlib/libz4 外部库）。 |
| 设备端 root（su） | 模块管理、dalvik-cache 清理等；无 su 时返回中文说明。 |
| 官方 Magisk / KernelSU 文件 | boot 修补的注入文件需用户从官方包中提取。 |

构建/测试：

```powershell
scripts\build.cmd -BuildType Debug -BuildDir build\extras
scripts\build.cmd -BuildType Debug -BuildDir build\extras -Target xmf_tests
build\extras\bin\xmf_tests.exe
```

---

## 7. 已知限制 / 后续

* `vendor_boot` 分段 ramdisk 修补、APatch 内核补丁、移除解锁红字、bootloader 参数备份、
  各品牌 FRP/网络锁官方流程 UI：以 `addPlanned` 占位，后续按机型适配。
* boot 修补只做 ramdisk init 替换；GKI KernelSU 请使用官方 LKM 方案。
* 模块管理依赖 Magisk/KernelSU 提供的 `magisk --install-module` 或 `/data/adb/modules` 标志，
  不同版本行为可能不同，失败时输出原始错误。
* 测试覆盖纯解析与镜像修补逻辑；真实设备流程需在真机上按文档验证。
