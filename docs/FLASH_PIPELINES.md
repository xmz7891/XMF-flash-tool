# XMF Flash Tool — 刷写流水线（src/flows）

本模块把**协议层**（`src/usb`、`src/qcom`、`src/mtk`、`src/sprd`、`src/rk`、`src/aw`）
与**引导包**（`src/packets`）串成面向工作台的刷写流水线：备份 / 恢复 / 读分区 / 写分区 /
擦分区 / 偏移读写 / 校验 / A-B 启动槽 / Loader 切换。所有动作都注册成 `ToolSpec`
（`category` 复用 qcom/mtk/sprd/rk/aw），由 `TaskContext` 报进度与取消。

核心约束：

* 分区表**动态获得**，不接受在代码里写死的分区列表：
  * QCOM / RK：进入 Firehose / RockUSB 后读取设备 **GPT**；
  * MTK：解析固件自带的 **scatter**；
  * SPRD：解析 **PAC 镜像表** + 分区地址表；
  * AW：FEL 没有分区表接口，使用用户提供的**分区地址表**（best effort）。
* 备份文件命名固定为 `<platform>_<partition>_<yyyyMMdd_HHmmss>.img`，写在用户选择的目录；
  同名时自动追加序号。
* 协议/存储访问统一通过 `PartitionIo` 抽象（`FlowOps.h`），因此备份/恢复/擦除/校验逻辑
  只写一次，五个平台行为一致，且可以用 `MemoryPartitionIo` 在无硬件环境离线测试。
* 危险操作（写分区、擦分区、按 scatter 刷写、刷 preloader）必须勾选 `confirm` 才会执行。

## 1. 目录与公共数据结构

| 文件 | 内容 |
| --- | --- |
| `PartitionTable.h/.cpp` | `PartitionInfo` / `PartitionTable`、GPT 解析、scatter/PAC/parameter/分区地址表转换、备份命名 |
| `FlowOps.h/.cpp` | `PartitionIo` 接口、通用备份/恢复/擦除/校验、`MemoryPartitionIo` |
| `QcomFlow.h/.cpp` | `QcomSession`（Sahara+Firehose）、`QcomFirehoseIo` |
| `MtkFlow.h/.cpp` | `MtkSession`（BROM→DA1→DA2）、`MtkDaStorageIo`、scatter 刷写计划 |
| `SprdFlow.h/.cpp` | `SprdSession`（FDL1→FDL2）、`SprdFdlStorageIo`、PAC 装载与地址表 |
| `RkFlow.h/.cpp` | `RkSession`/`RkStorageIo`（RockUSB，GPT/parameter.txt） |
| `AwFlow.h/.cpp` | `AwSession`/`AwMemoryIo`（FEL 内存读写 + 地址表） |
| `FlowTools.cpp` | 全部 `ToolSpec` 注册与 handler |
| `tests/test_flow_*.cpp` | MockTransport 回放 + 合成分区表测试 |

`PartitionTable` 的 `toVariantList()` 直接输出 QML 可用的 `QVariantList`（每个分区含
`name/index/offset/size/offsetHex/sizeHex/type/fileName/hasAddress/isDownload/slot`），
供后续工作台接入时显示，不需要在 QML 里再解析。

## 2. Qualcomm（EDL / Firehose）

流程：

```
openTransport(qcom-edl)
 → Sahara HELLO/HELLO_RESPONSE
 → 上传 programmer（引导包 Model.Loader 的 devprg ELF，或显式文件）
 → Firehose configure（MemoryName = ufs/emmc/spinor，由引导包 Storage 或用户选择）
 → 读 64 KiB 前缀，在 offset 512 / 4096 找 "EFI PART" 得到 GPT（扇区大小随之确定）
 → 备份 / 恢复 / 读 / 写 / 擦 / 偏移读写 / 校验 / A-B 槽
```

* **分区表**：`PartitionTable::parseGptPrefix` 同时兼容 512（eMMC）与 4096（UFS）逻辑扇区；
  分区偏移 = `firstLba * gptSectorSize`，大小 = `(lastLba-firstLba+1) * gptSectorSize`。
* **写分区**：Firehose `program` 以 1 MiB 分块；不足扇区尾部补零；非对齐偏移先做
  read-modify-write，避免破坏相邻字节。
* **擦分区**：Firehose `erase`，按扇区对齐（含偏移所在的整扇区）。
* **A/B 槽**：读取 `misc` 前 2080 字节，将 `bootloader_message_ab.slot_suffix`
  （偏移 **2048**，32 字节）写为 `_a` / `_b`，再写回。**best effort**：仅对使用标准
  BCB 的 bootloader 有效，厂商定制 bootloader 需另做适配（TODO）。
* **校验和**：读取整分区后计算 CRC32 或 SHA-256，可与外部文件比对。
* **预置擦除**：`userdata / frp / metadata / misc` 勾选框，仅当 GPT 中存在同名分区时加入
  擦除列表。
* **失败场景**：
  * 9008 不在 EDL → `proto::openTransport` 抛「未检测到 9008 端口，请确认设备已进入 EDL
    模式并安装驱动」；
  * programmer 与机型不匹配 → Sahara HELLO 版本/长度异常或 Firehose configure NAK；
  * 存储类型选错 → `configure` NAK 或读 GPT 全零；
  * 写越界（镜像大于分区）→ 日志告警，设备可能 NAK。

对应工具：`qcom.flow.partitions/backup/restore/read/write/erase/raw/slot/checksum`。

## 3. MediaTek（BROM → DA1 → DA2）

流程：

```
openTransport(mtk-brom)
 → BROM 0xA0/0x5A 握手
 → SEND_DA(DA1, DA1Address) + JUMP_DA
 → 等待设备重新枚举为 MTK DA 端口（默认 15 s）
 → SEND_DA(DA2, DA2Address) + JUMP_DA
 → 按 scatter 分区表执行备份/恢复/读/写/擦/批量刷写/preloader
```

* **分区表 = scatter**（固件随包提供，解析器在 `src/mtk/Scatter.*`）：
  `physical_start_addr` 作为分区字节偏移，`partition_size` 作为大小。
* **地址**：DA1 默认 `0x200000`、DA2 默认 `0x40000000`；引导包 Model 的
  `DA1Address/DA2Address/DA1/DA2` 会被 `MtkOptions::fromPacket` 自动填入。
  `signature_length` 默认 0（未签名 DA）。
* **批量刷写**：`buildScatterFlashPlan()` 只收集 `is_download=true` 且 `file_name` 非
  `NONE` 的分区；固件目录默认为 scatter 所在目录；文件缺失会在开始前一次性报错。
* **preloader**：备份/刷写 `scatter` 中的 `preloader` 分区（刷写需确认）。
* **失败场景**：BROM 握手超时、DA 文件损坏/地址错误（跳转后不再枚举）、scatter 与 DA
  版本不匹配（存储读写状态非 0）、固件文件缺失、擦除/写入被安全策略拒绝。
* **TODO（重要）**：`src/mtk` 目前只暴露 BROM/DA 命令层，没有 DA2 存储读写接口。
  为不阻塞流水线，本模块使用 `MtkDaStorageIo` 的实验性帧格式：
  ```
  请求：[cmd u8][len u32 BE][payload]     响应：[status u8][len u32 BE][data]
  cmd = 0x01 读（payload: addr u64 BE, size u32 BE）
        0x02 写（payload: addr u64 BE, size u32 BE；随后写原始数据）
        0x03 擦（payload: addr u64 BE, size u32 BE）
  ```
  该格式只用于把流水线逻辑跑通（Mock 测试覆盖），**尚未在真机校验**；实机联调时应把
  `MtkDaStorageIo::command()` 换成 DA2 真实的存储命令（或由 src/mtk 提供正式接口）。

对应工具：`mtk.flow.partitions/backup/restore/read/write/erase/flash/preloader`。

## 4. Spreadtrum / Unisoc（U2S / FDL1 → FDL2）

流程：

```
openTransport(sprd-fdl)
 → FDL CONNECT + READ_VERSION
 → START/MIDST/END/EXEC 下载 FDL1（地址默认 0x65000800）
 → 等待重新枚举
 → 下载并执行 FDL2（地址默认 0x9efffe00）
 → PAC 解析 → 套用分区地址表 → 按镜像表刷写 / 回读 / 擦除 / 备份
```

* **PAC**：头部 `"PAC\0"` + 版本/头长/镜像数/镜像表偏移/总大小；镜像表项 0x40 字节
  （type/offset/size/crc32/name[32]）。解析见 `src/sprd/Pac.*`。
* **地址表**：PAC 不含存储地址，必须提供文本文件，每行 `name,offset,size`
  （支持 `0x` 十六进制）；解析后 PAC 的镜像名与地址表按名称匹配。
* **写**：`0x0E WRITE_RAW_DATA`（addr u32 + size u32 + data，32 KiB 分块）；
  **读**：`0x0D READ_RAW_DATA`（响应帧携带数据，或裸流）；
  **擦**：`0x0A ERASE_FLASH`。
* **失败场景**：FDL1 后不重枚举、FDL2 版本与固件不匹配、PAC 镜像名与地址表对不上、
  地址超出 32 位、U2S CRC 错误或超时。
* **TODO**：FDL2 存储命令的 status 语义与 `READ_RAW_DATA` 是否分帧需真机校验；
  PAC 零售版本可能带额外头/加密层。

对应工具：`sprd.flow.partitions/flash/backup/read/erase`。

## 5. Rockchip（RockUSB）

流程：

```
openTransport(rk, VID 2207)
 → DEVICE_INFO（版本/芯片）
 → 分区表：优先 parameter.txt（离线解析 mtdparts），否则读设备 GPT（512 字节扇区）
 → 备份/恢复/读/写/擦/偏移读写
 → SET_RESET_FLAG：0 普通重启，1 重启到 Loader
```

* **parameter.txt**：`CMDLINE: ... mtdparts=rk29xxnand:size@offset(name),...`，
  size/offset 单位为 512 字节扇区。`PartitionTable::fromParameterText` 解析。
* **GPT**：通过 RockUSB `READ_SECTOR(0,64,512)` 读取 32 KiB，再走统一 GPT 解析；
  设备没有 GPT 时提示改用 parameter.txt。
* **写**：扇区对齐要求由 RockUSB 保证，流水线对非对齐偏移做 read-modify-write。
* **失败场景**：MaskROM/Loader 模式不对（DEVICE_INFO 超时）、GPT 不存在、
  parameter 与实际设备不一致、写保护/分区越界（CSW status≠0）。
* **TODO**：Rockchip 的 vendor 分区（loader/parameter 等）并没有真实 GPT，建议固件刷写
  始终提供 parameter.txt；DEVICE_INFO 结构随 Loader 版本变化（详见 PROTOCOLS.md）。

对应工具：`rk.flow.partitions/backup/restore/read/write/erase/raw/loader`。

## 6. Allwinner（FEL）

流程：

```
openTransport(aw-fel)
 → FEL_VER / HELLO（AWUSBFEX + SoC id）
 → 分区地址表（name,offset,size）→ 备份 / 恢复 / 擦除（写零模拟）
 → FEL READ / WRITE / EXEC（按地址）
```

* FEL 没有分区表/擦除命令：分区操作必须提供地址表；擦除通过写零实现（best effort）。
* 地址 32 位；超过 `0xFFFFFFFF` 会明确报错。
* **失败场景**：设备不在 FEL、地址表缺失、SoC 处于 secure boot 拒绝执行、写地址越界。
* **TODO**：PhoenixSuit 的分区语义来自固件包内分区表/`sunxi_mbr`，本模块暂未解析该格式，
  仅支持显式地址表；后续可增加 `.img` 包解析。

对应工具：`aw.flow.hello/partitions/backup/restore/read/write/exec`。

## 7. 工具清单（category = qcom / mtk / sprd / rk / aw，group = flow）

| 平台 | 工具 id | 说明 |
| --- | --- | --- |
| QCOM | `qcom.flow.partitions` | 读设备 GPT（可导出 CSV） |
| QCOM | `qcom.flow.backup` | 全部/指定分区备份 |
| QCOM | `qcom.flow.restore` | 从备份目录恢复 |
| QCOM | `qcom.flow.read` / `write` | 单分区读/写（写需确认） |
| QCOM | `qcom.flow.erase` | 指定分区或 userdata/frp/metadata/misc 预设（需确认） |
| QCOM | `qcom.flow.raw` | 按字节偏移读写 |
| QCOM | `qcom.flow.slot` | 设置 A/B 启动槽 |
| QCOM | `qcom.flow.checksum` | CRC32/SHA-256 校验（可比对文件） |
| MTK | `mtk.flow.partitions` | 解析 scatter（离线） |
| MTK | `mtk.flow.backup/restore/read/write/erase` | scatter 分区操作 |
| MTK | `mtk.flow.flash` | 按 scatter 批量刷写固件目录（需确认） |
| MTK | `mtk.flow.preloader` | preloader 备份/刷写 |
| SPRD | `sprd.flow.partitions` | PAC 镜像表 + 地址表（离线） |
| SPRD | `sprd.flow.flash/backup/read/erase` | PAC/地址表分区操作 |
| RK | `rk.flow.partitions` | GPT 或 parameter.txt |
| RK | `rk.flow.backup/restore/read/write/erase/raw` | RockUSB 分区操作 |
| RK | `rk.flow.loader` | 普通重启 / 切换 Loader |
| AW | `aw.flow.hello` | FEL 握手 |
| AW | `aw.flow.partitions` | 地址表（离线） |
| AW | `aw.flow.backup/restore/read/write/exec` | FEL 分区/内存操作 |

字段约定（UI 契约）：

* 目录类一律 `fields::dir`，文件类一律 `fields::path`；分区/偏移用文本（支持 `0x`）；
* 引导包联动用 `packet`（引导包 id）+ `model`（机型）两个可选文本字段，留空则使用显式
  DA/FDL/programmer 路径；
* 进度 0–100 由 `TaskContext`/`Reporter` 上报，取消通过 `checkCanceled()` 在每个分区/
  每个数据块前检查；无设备时抛统一中文错误（如「未检测到 9008 端口…」）。

## 8. 测试与无硬件验证

`src/flows/tests/` 全部使用合成数据 + `MockTransport`，不依赖真机/真固件：

* `test_flow_partitions`：GPT(512/4096)、scatter、parameter.txt、PAC、地址表、分区名
  查找（含 `_a/_b` 回退）、备份命名、`MemoryPartitionIo` 备份/恢复/擦除/CRC32；
* `test_flow_qcom`：Sahara 上传 → Firehose configure → GPT 读取 → 读分区/写分区/擦除/
  A-B 槽（校验写出的 `<program>` / `<erase>` 与 misc 槽位字节）；
* `test_flow_mtk`：scatter 表、DA 读/写/擦帧、错误状态、preloader 备份、scatter 刷写计划
  （缺失文件报错 / 正常刷写）；
* `test_flow_sprd`：PAC 解析、地址表套用、FDL 写/读/擦帧、`flashPac`、缺地址报错；
* `test_flow_rk`：DEVICE_INFO、GPT 读取、parameter.txt、读写擦、备份、Loader 切换；
* `test_flow_aw`：FEL 版本、READ/WRITE/EXEC、地址表备份、写零擦除、缺地址表报错；
* `test_flow_tools`：37 个工具全部注册且可运行、目录/文件/确认字段符合 UI 契约。

## 9. TODO / 待联调

1. MTK DA2 存储读写：把 `MtkDaStorageIo` 的实验帧替换为真实 DA 协议（或推动 src/mtk
   提供官方接口）。
2. SPRD FDL2 的 `READ_RAW_DATA` / `WRITE_RAW_DATA` 状态与分帧方式真机校验。
3. QCOM A-B 槽：厂商 bootloader 的 BCB 变体（部分机型写 `misc` 之外区域）。
4. Rockchip loader/parameter 分区的特殊处理；固件包（RKFW）解析。
5. Allwinner sunxi_mbr / PhoenixSuit 分区表解析，替代手工地址表。
6. 大批量备份的速度优化（并发读、ZIP 打包）与断点续备。
