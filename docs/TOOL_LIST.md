# 工具清单

本文件由 `XMF.exe --dump-tools` 自动生成，共 **220** 个工具（可运行 **220**）。

## 固件 & 镜像（image）· 52 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `firmware.payloadDumperGo` | Payload 解包（开源工具） | 是 |
| `image.boot.info` | Boot 镜像信息 | 是 |
| `image.boot.repack` | Boot 重打包 | 是 |
| `image.boot.unpack` | Boot 解包 | 是 |
| `image.bootanimation.extract` | 开机动画提取 | 是 |
| `image.bootlogo.extract` | BootLogo 提取 | 是 |
| `image.bootlogo.pack` | BootLogo 打包 | 是 |
| `image.convert.batchScan` | 镜像批量扫描/导出 | 是 |
| `image.convert.bin2img` | BIN 转 IMG | 是 |
| `image.convert.detect` | 镜像类型识别 | 是 |
| `image.convert.gz2img` | GZ 转 IMG | 是 |
| `image.convert.img2bin` | IMG 转 BIN | 是 |
| `image.convert.img2gz` | IMG 转 GZ | 是 |
| `image.convert.img2lzo` | IMG 转 LZO | 是 |
| `image.convert.lzo2img` | LZO 转 IMG | 是 |
| `image.convert.number` | 十六进制 / 十进制转换 | 是 |
| `image.convert.zstd` | 镜像 zstd 解压 | 是 |
| `image.cpio.pack` | cpio 打包 | 是 |
| `image.cpio.unpack` | cpio 解包 | 是 |
| `image.elf.info` | ELF 解析 | 是 |
| `image.elf.symbols` | 内核符号表 | 是 |
| `image.gpt.backup` | GPT 备份 | 是 |
| `image.gpt.extract` | 按 GPT 提取分区 | 是 |
| `image.gpt.info` | GPT 解析 | 是 |
| `image.gpt.repair` | GPT 修复 | 是 |
| `image.gpt.restore` | GPT 恢复 | 是 |
| `image.hash.file` | 文件哈希校验 | 是 |
| `image.init_boot.repack` | init_boot 重打包 | 是 |
| `image.init_boot.unpack` | init_boot 解包 | 是 |
| `image.partition.extract` | 按偏移提取 | 是 |
| `image.payload.applyDiff` | Payload 增量分析 | 是 |
| `image.payload.info` | Payload 信息 | 是 |
| `image.payload.pack` | Payload 打包 | 是 |
| `image.payload.unpack` | Payload 解包 | 是 |
| `image.ramdisk.modify` | Ramdisk 修改 | 是 |
| `image.scan.blank` | 空白块扫描 | 是 |
| `image.sparse.info` | Sparse 信息 | 是 |
| `image.sparse.pack` | Sparse 打包 | 是 |
| `image.sparse.unpack` | Sparse 解包 | 是 |
| `image.super.info` | Super 信息 | 是 |
| `image.super.merge` | Super 合并 | 是 |
| `image.super.resize` | Super 扩容/缩容 | 是 |
| `image.super.split` | Super 拆分 | 是 |
| `image.vbmeta.disable` | VBMeta 关闭校验 | 是 |
| `image.vbmeta.info` | VBMeta 信息 | 是 |
| `image.vbmeta.verify` | DM-verity 校验 | 是 |
| `image.vendor_boot.info` | vendor_boot 信息 | 是 |
| `image.vendor_boot.repack` | vendor_boot 重打包 | 是 |
| `image.vendor_boot.unpack` | vendor_boot 解包 | 是 |
| `image.zip.extract` | 卡刷包解压 | 是 |
| `image.zip.info` | 卡刷包解析 | 是 |
| `image.zip.verify` | 卡刷包校验 | 是 |

## 高通 9008（qcom）· 18 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `qcom.elf.parse` | devprg / ELF 解析 | 是 |
| `qcom.firehose.erase` | Firehose 擦除扇区 | 是 |
| `qcom.firehose.info` | Firehose 存储信息 | 是 |
| `qcom.firehose.patch` | Firehose 补丁 | 是 |
| `qcom.firehose.program` | Firehose 写入扇区 | 是 |
| `qcom.firehose.read` | Firehose 读取扇区 | 是 |
| `qcom.flow.backup` | QCOM 分区备份 | 是 |
| `qcom.flow.checksum` | QCOM 分区校验 | 是 |
| `qcom.flow.erase` | QCOM 擦除分区 | 是 |
| `qcom.flow.partitions` | QCOM 读取分区表 | 是 |
| `qcom.flow.raw` | QCOM 偏移读写 | 是 |
| `qcom.flow.read` | QCOM 读取分区 | 是 |
| `qcom.flow.restore` | QCOM 分区恢复 | 是 |
| `qcom.flow.slot` | QCOM 设置启动槽 | 是 |
| `qcom.flow.write` | QCOM 写入分区 | 是 |
| `qcom.ports.scan` | 检测 9008 端口 | 是 |
| `qcom.sahara.handshake` | Sahara 握手 | 是 |
| `qcom.sahara.upload` | Sahara 上传 programmer | 是 |

## 联发科底层（mtk）· 15 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `mtk.brom.handshake` | BROM 握手 | 是 |
| `mtk.brom.read32` | BROM 读取 32 位 | 是 |
| `mtk.da.load` | 加载 DA1 / DA2 | 是 |
| `mtk.da.memory` | DA 内存读写 | 是 |
| `mtk.flow.backup` | MTK 分区备份 | 是 |
| `mtk.flow.erase` | MTK 擦除分区 | 是 |
| `mtk.flow.flash` | MTK 按 scatter 刷写 | 是 |
| `mtk.flow.partitions` | MTK 解析 scatter | 是 |
| `mtk.flow.preloader` | MTK preloader 备份/刷写 | 是 |
| `mtk.flow.read` | MTK 读取分区 | 是 |
| `mtk.flow.restore` | MTK 分区恢复 | 是 |
| `mtk.flow.write` | MTK 写入分区 | 是 |
| `mtk.ports.scan` | 检测 MTK 端口 | 是 |
| `mtk.preloader.parse` | Preloader 解析 | 是 |
| `mtk.scatter.parse` | Scatter 解析 | 是 |

## Bootloader（bootloader）· 11 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `bootloader.backup.env` | Bootloader 参数备份 | 是 |
| `bootloader.getvar` | 读取 Fastboot 变量 | 是 |
| `bootloader.info` | Bootloader 信息 | 是 |
| `bootloader.redtext` | 解锁红字检测与说明 | 是 |
| `bootloader.setvar` | 写入 Bootloader 变量 | 是 |
| `bootloader.slots` | A/B 槽位 | 是 |
| `bootloader.unlock` | Bootloader 解锁 / 回锁 | 是 |
| `bootloader.verify` | 引导校验状态 | 是 |
| `device.driver.install` | 安装驱动 | 是 |
| `device.drivers` | 驱动状态检测 | 是 |
| `device.ports` | 端口扫描 | 是 |

## 紫光展锐（sprd）· 9 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `sprd.fdl.load` | 加载 FDL1 / FDL2 | 是 |
| `sprd.flow.backup` | SPRD 分区备份 | 是 |
| `sprd.flow.erase` | SPRD 擦除分区 | 是 |
| `sprd.flow.flash` | SPRD 刷写 PAC | 是 |
| `sprd.flow.partitions` | SPRD PAC 镜像表 | 是 |
| `sprd.flow.read` | SPRD 读取分区 | 是 |
| `sprd.pac.parse` | PAC 解析 / 解包 | 是 |
| `sprd.ports.scan` | 检测展锐端口 | 是 |
| `sprd.u2s.handshake` | U2S 握手 | 是 |

## 瑞芯微 RK（rk）· 16 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `rk.device.info` | RockUSB 设备信息 | 是 |
| `rk.erase` | 擦除扇区 | 是 |
| `rk.firmware.info` | RK 固件解析 | 是 |
| `rk.firmware.unpack` | RK 固件解包 | 是 |
| `rk.flow.backup` | RK 分区备份 | 是 |
| `rk.flow.erase` | RK 擦除分区 | 是 |
| `rk.flow.loader` | RK 重启 / 切换 Loader | 是 |
| `rk.flow.partitions` | RK 分区表 | 是 |
| `rk.flow.raw` | RK 偏移读写 | 是 |
| `rk.flow.read` | RK 读取分区 | 是 |
| `rk.flow.restore` | RK 分区恢复 | 是 |
| `rk.flow.write` | RK 写入分区 | 是 |
| `rk.ports.scan` | 检测 Rockchip 端口 | 是 |
| `rk.read` | 读取扇区 | 是 |
| `rk.reset` | 重启 / 切换 Loader | 是 |
| `rk.write` | 写入扇区 | 是 |

## 全志 A 系列（aw）· 14 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `aw.fel.hello` | FEL 握手 / 版本 | 是 |
| `aw.fel.read` | FEL 读取内存 | 是 |
| `aw.fel.run` | FEL 下载并执行 | 是 |
| `aw.fel.write` | FEL 写入内存 | 是 |
| `aw.firmware.info` | 全志固件解析 | 是 |
| `aw.firmware.unpack` | 全志固件解包 | 是 |
| `aw.flow.backup` | 全志分区备份 | 是 |
| `aw.flow.exec` | FEL 执行地址 | 是 |
| `aw.flow.hello` | FEL 握手 | 是 |
| `aw.flow.partitions` | 全志分区地址表 | 是 |
| `aw.flow.read` | FEL 读取内存 | 是 |
| `aw.flow.restore` | 全志分区恢复 | 是 |
| `aw.flow.write` | FEL 写入 / 执行 | 是 |
| `aw.ports.scan` | 检测 FEL 端口 | 是 |

## Root / KSU / 面具（root）· 7 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `root.apatch.patch` | APatch 内核检查 / 备份 | 是 |
| `root.boot.patch` | Boot 镜像修补 | 是 |
| `root.hide` | Root 痕迹检测 / 隐藏检查 | 是 |
| `root.module` | 模块管理 | 是 |
| `root.modules` | 模块列表 | 是 |
| `root.prop.patch` | ramdisk 属性修改 | 是 |
| `root.status` | Root 状态检测 | 是 |

## 账号锁 / 清除（unlock）· 8 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `unlock.account.official` | 账号锁官方移除流程 | 是 |
| `unlock.cache` | 擦除 cache | 是 |
| `unlock.dalvik` | 清除 dalvik-cache | 是 |
| `unlock.frp` | 擦除 FRP 分区 | 是 |
| `unlock.metadata` | 清除 metadata | 是 |
| `unlock.sim.official` | 运营商网络锁官方解锁 | 是 |
| `unlock.simlock` | SIM / 运营商状态 | 是 |
| `unlock.userdata` | 格式化 userdata | 是 |

## ADB / Fastboot（adb）· 32 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `adb.apk.extract` | 导出应用 APK | 是 |
| `adb.app.control` | 应用冻结 / 卸载 | 是 |
| `adb.bugreport` | 抓取 bugreport | 是 |
| `adb.device.report` | 导出设备硬件信息 | 是 |
| `adb.devices` | ADB 设备列表 | 是 |
| `adb.dmesg` | 导出内核日志 | 是 |
| `adb.forward` | 端口转发 | 是 |
| `adb.info` | 读取设备信息 | 是 |
| `adb.install` | 安装 APK | 是 |
| `adb.logs` | 导出设备日志 | 是 |
| `adb.multiShell` | 多设备并行命令 | 是 |
| `adb.partitions` | 读取分区表 | 是 |
| `adb.pull` | 拉取文件 | 是 |
| `adb.push` | 推送文件 | 是 |
| `adb.reboot` | ADB 重启设备 | 是 |
| `adb.server` | ADB 服务管理 | 是 |
| `adb.shell` | ADB 命令执行 | 是 |
| `adb.sideload` | Recovery 卡刷（sideload） | 是 |
| `adb.waitDevice` | 等待设备上线 | 是 |
| `adb.wireless` | 无线调试 | 是 |
| `fastboot.battery` | 电池与充电状态 | 是 |
| `fastboot.devices` | Fastboot 设备列表 | 是 |
| `fastboot.erase` | 擦除分区 | 是 |
| `fastboot.flash` | 刷写分区 | 是 |
| `fastboot.flash.check` | 刷写前检查 | 是 |
| `fastboot.getvar` | 读取 Fastboot 变量 | 是 |
| `fastboot.info` | 导出 Fastboot 信息 | 是 |
| `fastboot.lock` | Bootloader 解锁 / 上锁 | 是 |
| `fastboot.oem` | OEM 命令 | 是 |
| `fastboot.partitions` | 读取分区表 | 是 |
| `fastboot.partitions.export` | 导出分区表 CSV | 是 |
| `fastboot.reboot` | Fastboot 重启 | 是 |

## 固件资源（firmware）· 18 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `firmware.dataset.info` | 固件数据集信息 | 是 |
| `firmware.file.hash` | 固件哈希校验 | 是 |
| `firmware.file.identify` | 固件类型识别 | 是 |
| `firmware.file.matchModel` | 机型数据库匹配 | 是 |
| `firmware.file.mergeSplit` | 固件分卷合并 | 是 |
| `firmware.file.unpack` | 固件自动解包 | 是 |
| `firmware.file.unpackOfp` | OFP 解包 | 是 |
| `firmware.file.unpackOzip` | OZIP 解包 | 是 |
| `firmware.official.lookup` | 品牌官方固件入口 | 是 |
| `firmware.xiaomi.download` | 小米 / 红米固件下载 | 是 |
| `firmware.xiaomi.otaCheck` | MIUI OTA 接口查询 | 是 |
| `firmware.xiaomi.search` | 小米 / 红米固件检索 | 是 |
| `miflash.flash` | MiFlash 线刷 | 是 |
| `miflash.inspect` | 解析小米线刷包 | 是 |
| `packets.export` | 导出机型清单 | 是 |
| `packets.models` | 引导包机型列表 | 是 |
| `packets.resolve` | 解析引导包文件 | 是 |
| `packets.scan` | 引导包扫描 | 是 |

## 批量 / 自动化（batch）· 9 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `batch.encoding` | 批量编码转换 | 是 |
| `batch.queue` | 批量队列任务 | 是 |
| `batch.rename` | 批量重命名 | 是 |
| `batch.script` | 批量脚本执行 | 是 |
| `batch.timestamp` | 批量修改时间戳 | 是 |
| `batch.verify` | 批量校验文件 | 是 |
| `system.hash` | 文件哈希校验 | 是 |
| `system.info` | 系统信息采集 | 是 |
| `system.selfTest` | 环境自检 | 是 |

## 机型旁路 / 工程（bypass）· 7 个

| ID | 名称 | 可运行 |
| --- | --- | --- |
| `bypass.activities` | 工程模式入口列表 | 是 |
| `bypass.carrier.guide` | 运营商/厂商工程权限说明 | 是 |
| `bypass.codes` | 工程模式拨号指令 | 是 |
| `bypass.deviceinfo` | 机型 / 出厂信息 | 是 |
| `bypass.dial` | 打开工程模式拨号 | 是 |
| `bypass.factory` | 工程模式启动 | 是 |
| `bypass.frp.guide` | FRP 官方恢复流程说明 | 是 |
