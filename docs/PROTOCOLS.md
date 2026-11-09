# XMF Flash Tool — 五平台协议层说明

本文档记录 `src/usb`、`src/qcom`、`src/mtk`、`src/sprd`、`src/rk`、`src/aw` 的
帧格式、命令码、地址约定、限制与待验证项。所有内容由 `src/<模块>/tests/` 下的
MockTransport 回放测试覆盖，**不需要真实硬件即可构建与测试**。

## 0. 分层与约定

```
工具 handler (TaskContext)
        │  proto::Reporter（进度/日志/取消）
        ▼
协议实现 (sahara/firehose/brom/da/u2s/fdl/rockusb/fel)
        │  只依赖 gf::usb::IUsbTransport
        ▼
WinUsbTransport（真实设备） / MockTransport（测试）
```

* 协议代码**不直接调用任何 OS USB API**；枚举通过 `usb::enumerateFlashPorts()`，
  传输通过 `usb::createTransport(port)` + `IUsbTransport`。
* 进度、日志、取消统一走 `gf::proto::Reporter`；工具 handler 用
  `proto::reporterFor(TaskContext&)` 适配，取消以 `proto::Canceled` 抛出并在工具层
  转成 `TaskContext::TaskCanceled`。
* 公共 I/O 助手在 `src/usb/UsbIo.h`（`readExact` / `writeAll` / `readUntil`），
  所有字节序/数值助手在 `src/usb/ProtocolSupport.h`。

## 1. src/usb — 传输层

### 1.1 设备识别（VID/PID → chipHint）

| chipHint | VID | PID | 说明 |
| --- | --- | --- | --- |
| `qcom-edl` | 0x05C6 | 0x9008 / 0x900E / 0x901D / 0x9091 / 0x9006 | 高通 EDL（Sahara/Firehose） |
| `mtk-brom` | 0x0E8D | 0x0003 | 联发科 BROM |
| `mtk-preloader` | 0x0E8D | 0x2000 | PreLoader/DA 重新枚举后的 USB 口 |
| `mtk-da` | 0x0E8D | 0x2001 | DA 模式 |
| `sprd-fdl` | 0x1782 | 任意 | 展锐 U2S / FDL |
| `rk` | 0x2207 | 任意 | 瑞芯微 MaskROM / Loader |
| `aw-fel` | 0x1F3A | 0xEFE8（其它 PID 也接受） | 全志 FEL |

`chipHintForIds()` 返回上表字符串，`platformForHint()` 再映射到 `ChipPlatform`。

### 1.2 WinUsbTransport

* 用 SetupAPI（`GUID_DEVINTERFACE_USB_DEVICE`）枚举设备接口，从硬件 ID
  `USB\VID_xxxx&PID_xxxx` 解析 VID/PID，只保留上表支持的设备。
* `open()`：`CreateFileW(..., FILE_FLAG_OVERLAPPED)` → `WinUsb_Initialize` →
  扫描 bulk IN/OUT 端点。**必须绑定 WinUSB 驱动**，否则返回可读中文错误。
* 每次读写/控制传输都是 OVERLAPPED + `WaitForSingleObject`，超时后
  `CancelIoEx`；没有无限阻塞。
* `read()` 只做一次 bulk 传输并返回实际字节数，长读取由 `usb::readExact` 循环。
* 枚举的 `busNumber` / `address` 暂时为 0（可由设备实例 ID 解析，TODO）。

### 1.3 MockTransport

`src/usb/MockTransport.h`，用于全部协议回放测试：

```cpp
gf::usb::MockTransport usb;
usb.queueRead(sahara::buildHelloPacket());     // 下一次 read() 返回的数据
usb.setWriteHook([](const QByteArray& req){ return deviceReply(req); });
sahara::Client client(usb);
auto hello = client.handshake();
QCOMPARE(usb.writes().first(), expectedHelloResponse);
```

* 读取队列是字节块 FIFO，块大于请求长度会被拆分（与真实 bulk 管道一致）。
* 空队列 = 超时（`read()` 返回空）。
* 记录所有 `writes()` / `controls()`，可以断言主机发出的完整帧序列。
* 可注入打开失败、写入失败、短写、控制传输失败。

## 2. src/qcom — 高通 Sahara + Firehose

### 2.1 Sahara 帧

所有包为小端：`u32 command; u32 length;` 后跟 `length-8` 字节负载。
**`length` 是包含 8 字节头的整包长度**，唯二例外是 `READ_DATA` / `READ_DATA_64`：
其 `length` 表示目标请求的镜像字节数（包本身无负载）。

| 命令 | 值 | 方向 | 负载 |
| --- | --- | --- | --- |
| HELLO | 0x01 | 目标→主机 | `version, min_version, max_cmd_length, mode, reserved[6]`（共 48 字节） |
| HELLO_RESPONSE | 0x02 | 主机→目标 | 同上 48 字节 |
| READ_DATA | 0x03 | 目标→主机 | 无；`length` = 请求的镜像字节数 |
| END_IMAGE_TRANSFER | 0x04 | 主机→目标 | `image_id u32, status u32`（16 字节） |
| DONE | 0x05 | 目标→主机 | `image_id u32, status u32`（16 字节） |
| DONE_RESPONSE | 0x06 | 主机→目标 | 同上 |
| RESET / RESET_RESPONSE | 0x07 / 0x08 | 双向 | 无 |
| EXECUTE | 0x0D | 主机→目标 | `image_id u32, addr u32`（16 字节） |
| EXECUTE_RESPONSE | 0x0E | 目标→主机 | `image_id u32, status u32` |
| READ_DATA_64 | 0x12 | 目标→主机 | 同 READ_DATA |
| END_IMAGE_TRANSFER_64 | 0x13 | 主机→目标 | 同 0x04 |
| DONE_64 / DONE_RESPONSE_64 | 0x14 / 0x15 | 双向 | 同 0x05 / 0x06 |
| EXECUTE_64 / EXECUTE_RESPONSE_64 | 0x19 / 0x1A | 双向 | `image_id u32, addr u64`（20 字节） |

流程（`sahara::Client::upload`）：

1. 读 HELLO（版本 1–3；HELLO 长度 8–1024），回 HELLO_RESPONSE（版本取
   `min(设备版本, 3)`）→ 打印版本/模式。
2. 循环读包：
   * `READ_DATA(_64)` → 从镜像当前偏移写 `min(length, 剩余)` 字节，再发
     `END_IMAGE_TRANSFER(_64)`，更新进度；
   * `DONE(_64)` → 解析 `image_id/status`，回 `DONE_RESPONSE(_64)`，status=0 成功；
   * `EXECUTE_RESPONSE(_64)` → 返回成功/失败；
   * `READY_FOR_IMAGE` → 忽略；
   * 其它命令 → 明确报错「Sahara 未预期的命令」。
3. 超过镜像大小仍请求数据 → 报错，不会越界发送。

限制/注意：

* 主机不解析 ELF 段来发送数据：Sahara 的目标自己解析 ELF，主机只需按
  READ_DATA 顺序流式发送整个 programmer 文件。
* programmer 支持 `.elf` / `.mbn` / `devprg`；`qcom.elf.parse` 用
  `ElfImage::parse` 校验 ELF32/ELF64、程序头、PT_LOAD 段边界，仅用于校验和展示。
* **待硬件验证**：`END_IMAGE_TRANSFER/DONE/EXECUTE` 的字段（当前 image_id+status）
  来自公开 Sahara 文档与开源 EDL 客户端，不同 PBL 版本可能略有差异。

### 2.2 Firehose

* 命令是 UTF-8 XML，外层 `<data>…</data>`，可带
  `<?xml version="1.0" encoding="UTF-8" ?>`；`wrapCommand()` 统一包装。
* 响应 `<response value="ACK|NAK" .../>` 前可能有若干
  `<log value="..."/>`，可能跨多次 bulk 读，`readResponseXml()` 累积到
  `<response …/>` / `</response>` 完整为止。
* 已实现命令：

| 命令 | XML 示例 | 说明 |
| --- | --- | --- |
| configure | `<configure MemoryName="ufs" MaxPayloadSizeToTargetInBytes="1048576" .../>` | 选择存储、负载大小 |
| getstorageinfo | `<getstorageinfo physical_partition_number="0" />` | 存储信息（部分 programmer 不支持） |
| nop | `<nop />` | 保活/探测 |
| read | `<read SECTOR_SIZE_IN_BYTES="4096" physical_partition_number="0" start_sector="N" num_sectors="M" />` | 随后读 `M*4096` 字节 |
| program | `<program SECTOR_SIZE_IN_BYTES="4096" filename="x.bin" physical_partition_number="0" start_sector="N" num_partial_sectors="M" />` | 随后写原始扇区数据 |
| erase | `<erase SECTOR_SIZE_IN_BYTES="4096" physical_partition_number="0" start_sector="N" num_sectors="M" />` | |
| patch | `<patch SECTOR_SIZE_IN_BYTES="4096" byte_offset="O" physical_partition_number="0" size_in_bytes="L" start_sector="N" value="0x…" />` | 在扇区偏移处写少量字节（如修 GPT） |
| power | `<power value="reset" />` / `off` | 重启/关机 |

* `programSectors()` 会把不足一个扇区的尾部补零，按 1 MiB 分块；`ackAfterData`
  控制数据阶段后是否再期待一个 ACK（programmer 版本差异，默认 true）。
* `readSectors()` 默认只读命令 ACK + 数据；`trailingAck` 可打开尾随 ACK。
* 扇区大小默认 4096（eMMC/UFS）；`erasing/programming` 不做 GPT 解析，
  工具层以「起始扇区/扇区数」直接操作。

## 3. src/mtk — BROM / DA / PreLoader / Scatter

### 3.1 BROM 协议

* 握手：主机写单字节 `0xA0`，BROM 回 `0x5A`。
* 命令：1 字节 opcode + 大端负载；地址一律 `u32 BE`。

| opcode | 命令 | 请求负载 | 响应 |
| --- | --- | --- | --- |
| 0xD0 | READ16 | addr | 2 字节 |
| 0xD1 | READ32 | addr | 4 字节 |
| 0xD2 | WRITE16 | addr, value u16 | 2 字节回显（校验） |
| 0xD4 | WRITE32 | addr, value u32 | 4 字节回显（校验） |
| 0xD5 | JUMP_DA | addr | 无（设备重启到 DA） |
| 0xD6 | JUMP_BL | addr | 无 |
| 0xD7 | SEND_DA | addr, len, siglen, data… | 2 字节状态（0x0000 成功） |
| 0xD8 | GET_TARGET_CONFIG | 无 | 8 字节 |
| 0xDA | ECHO | value u32 | 4 字节回显 |
| 0xDB | JUMP | addr | 无 |

* `sendDa()` 数据按 64 KiB 分块写入并上报进度；DA1/DA2 加载后 `jumpDa()`。
* DA 固件复用同一套 opcode 做寄存器/内存访问（`mtk.da.memory` 工具）。
  DA 私有校验和封包尚未实现（TODO，见 §7）。

### 3.2 DA1 / DA2 地址

地址来自引导包 `Config.xml` 的 `Model` 属性（`_research` 样本）：

* `DA1Address` 常见 `2097152`（0x200000），个别机型 `16777216`（0x1000000）；
* `DA2Address` 常见 `1073741824`（0x40000000）；
* `DAPayloadAddress` 个别机型提供（如 2340200）。

工具 `mtk.da.load` / `mtk.da.memory` 的默认值即 0x200000 / 0x40000000，可覆盖。
DA1 跳转后设备重新枚举，`da::waitForDaPort()` 轮询等待（默认 15s），失败给出
可读错误。

### 3.3 PreLoader 头（真实样本验证）

`PreloaderInfo::parse` 解析：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0x00 | u32 | magic `0x014D4D4D`（低 24 位 `0x4D4D4D` = "MMM"） |
| 0x04 | u32 | 头长度，样本恒为 0x38（56） |
| 0x08 | char[12] | `"FILE_INFO"` |
| 0x20 | u32 | imageSize；多数等于文件大小，少数文件尾部多 256 字节签名 |
| 0x24 | u32 | 样本值 0x20000/0x40000/0x80000/0x100000/0xFFFFFFFF，**用途未确认，不当作加载地址** |
| 0x38 起 | 段链 | 每段 `u32 magic=0x014D4D4D, u16 length, u16 type, payload[length-8]`，直到出现非 MMM 数据（ARM boot code） |

样本段类型：1（12 字节）、7（100）、2（20）、8（48）。解析器会记录
`sections`、`sectionsEnd`，并对签名追加、异常长度给出 warning。已用
`_research/extract/package_mtk_edl-master/PreLoader/**/*.bin` 真实样本验证；
其中 `HuaweiHiEnjoy70Plus_preloader.bin` 是不同封装（magic 非 MMM），解析会明确报错。

### 3.4 Scatter 解析

支持 MTK scatter 的 `- key: value` 块结构（注释 `#`、空行、引号值均可），
提取 general（platform/project/storage/boot_channel/config_version/block_size）
与分区表（index/name/file_name/type/linear_start_addr/physical_start_addr/
partition_size/region/storage/boundary_check/is_reserved/operation_type）。
`mtk.scatter.parse` 可导出 CSV。加密/二进制 scatter 会明确报错而不是误解析。

## 4. src/sprd — U2S / FDL / PAC

### 4.1 U2S HDLC 帧

```
0x7E | type(u8) | length(u16 BE) | payload[length] | crc16(u16 BE) | 0x7E
```

* CRC-16/CCITT-FALSE：poly 0x1021，init 0xFFFF，覆盖 `type + length + payload`；
* 字节转义：`0x7E → 0x7D 0x5E`，`0x7D → 0x7D 0x5D`（XOR 0x20）；
* `u2s::Link` 负责粘包/拆包，帧间噪声会被跳过，CRC 错误明确报错。

### 4.2 FDL 命令

| 命令 | 值 | 请求负载 | 响应 |
| --- | --- | --- | --- |
| CONNECT | 0x00 | 无 | status（首字节 0 = OK） |
| START_DATA | 0x01 | addr u32, size u32, data_type u8 | status |
| MIDST_DATA | 0x02 | 数据块（≤64 KiB，16 KiB 分块发送） | 无 |
| END_DATA | 0x03 | checksum u32（0 时省略） | status |
| EXEC_DATA | 0x04 | addr u32 | status |
| NORMAL_RESET | 0x05 | 无 | 设备重启 |
| READ_FLASH | 0x06 | addr u32, size u32 | 数据（部分 FDL 用裸流） |
| READ_CHIP_TYPE | 0x07 | 无 | chip type u32 |
| ERASE_FLASH | 0x0A | addr u32, size u32 | status |
| READ_VERSION | 0x10 | 无 | ASCII 版本串 |

`fdl::Client::downloadFdl()` = START → MIDST* → END → EXEC，带进度。
`fdl::Client::rawCommand()` 是 FDL2 厂商扩展命令的逃生口（分区表等命令码随 FDL
版本不同，未硬编码）。
FDL1 启动后设备重新枚举，`fdl::waitForPort()` 轮询（默认 15s）。

**地址**：引导包 `Config.xml` 的 `FDL1Addr` / `FDL2Addr` / `PayloadAddr`（16 进制文本）；
样本机型为 `0x65000800` / `0x9efffe00` / `0x65015f08`，工具默认值引用前两个。

### 4.3 PAC 容器

结构见 `src/sprd/Pac.h`：头 0x100 字节（`"PAC\0"`、version、headerSize、
imageCount、tableOffset、totalSize），镜像表每项 0x40 字节
（type/offset/size/crc32/name[32]），数据紧随其后。解析器校验所有偏移/长度
边界，越界镜像跳过并 warning。`sprd.pac.parse` 可解包全部镜像。

**TODO**：零售 PAC 可能有额外头/加密层；结构按社区 PAC 解包器整理，
待真实 PAC 样本校验。

## 5. src/rk — RockUSB

### 5.1 CBW / CSW

CBW（31 字节小端）：`signature=0x43425355("USBC") u32, tag u32, dataLength u32,
flags u8(IN=0x80/OUT=0x00), lun u8, cbLength u8, cb[16]`。
CSW（13 字节）：`signature=0x53425355("USBS") u32, tag u32, residue u32,
status u8(0=成功)`。`Client::transfer()` 校验标签与 status。

扇区命令块：`cb[0]=opcode, cb[1]=0, cb[2..5]=起始扇区 LE u32,
cb[6..7]=扇区数 LE u16`；数据长度 = 扇区数 × 扇区大小（默认 512）。

| opcode | 命令 |
| --- | --- |
| 0x01 | READ_FLASH_ID |
| 0x04 / 0x05 / 0x06 | READ / WRITE / ERASE_SECTOR |
| 0x07 / 0x08 / 0x09 | READ / WRITE / EXEC_SDRAM |
| 0x0A | DEVICE_INFO（128 字节，版本 u32 + 可打印芯片串） |
| 0x0B / 0x0C | READ_CAPABILITY / READ_FLASH_INFO |
| 0x14 / 0x15 / 0x16 | READ / WRITE / ERASE_LBA（64 位 LBA） |
| 0x1A | READ_CHIP_INFO |
| 0x1B | SET_RESET_FLAG（0 普通重启，1 重启到 Loader） |

**TODO**：DEVICE_INFO 具体结构、RESET 参数位置、LBA 参数布局随 Loader 版本变化，
待硬件验证；MaskROM 与 Loader 的 PID 映射仅做通用命名。

## 6. src/aw — 全志 FEL

请求 16 字节小端：`request u32, length u32, address u32, reserved u32`；
数据阶段后跟 8 字节响应 `status u32, tag u32`（status=0 成功）。

| request | 值 | 说明 |
| --- | --- | --- |
| FEL_VER / HELLO | 0x001 | 读 32 字节版本块：`"AWUSBFEX"` + version u32 + SoC id u32 + firmware u32[4] |
| FEL_READ | 0x011 | 从 address 读 length 字节 |
| FEL_WRITE | 0x012 | 写 length 字节到 address（主机→设备） |
| FEL_EXEC | 0x013 | 跳转执行 address |
| FEL_RUN | 0x014 | 预留 |
| FEL_NOTIFY | 0x015 | 预留 |

* 按 16 KiB 分块（BootROM 单次 bulk 限制），每次请求地址递增。
* SoC id 常见值：A10=0x1623、A13=0x1625、A20=0x1651、A64=0x1689、H3=0x1680、
  H5=0x1718、H6=0x1728（未知显示十六进制）。
* **TODO**：部分 BROM 响应只有 4 字节 status；客户端已接受短响应，但仍需硬件确认。

## 7. 无硬件测试与真实样本

MockTransport 回放测试覆盖：

* `src/usb/tests/`：VID/PID 识别、Mock 队列/短写/超时/控制传输、Reporter、工具注册表；
* `src/qcom/tests/`：Sahara 握手/上传/64 位/DONE/EXECUTE 帧序列；Firehose XML
  与响应解析、分片响应、读/写/擦除；真实 devprg ELF 与 MBN 解析、真实 ELF 流式上传；
* `src/mtk/tests/`：BROM 握手/读写/回显校验/SEND_DA；真实 preloader 解析
  （Xiaomi/OPPO 样本）；scatter 解析与 CSV；
* `src/sprd/tests/`：CRC 已知向量、转义/组帧/CRC 校验、Link 收发、FDL 下载帧序列；
  合成 PAC 的解析与镜像提取；
* `src/rk/tests/`：CBW/CSW 编解码、读/写/擦除/重启帧序列、失败与标签不匹配；
* `src/aw/tests/`：请求/响应/版本块、HELLO/READ/WRITE 分块/EXEC。

真实样本只读引用 `_research/`（缺失时测试自动 `QSKIP`，不会失败）：

* `package_qualcomm_edl-master/Common/prog_ufs_firehose_sdm845.elf`、`8909.mbn`；
* `package_mtk_edl-master/PreLoader/**/*.bin`、`DA/**/*.bin`；
* `Config.xml` 中的 DA1/DA2/FDL 地址与机型结构。

运行：

```powershell
scripts\build.cmd -BuildType Debug -BuildDir build\proto -Target xmf_tests
build\proto\bin\xmf_tests.exe
```

## 8. 待验证 / TODO 汇总

1. Sahara `END_IMAGE_TRANSFER` / `DONE` / `EXECUTE` 的负载字段与真实 9008 设备核对。
2. Firehose `program` 数据阶段 ACK 数量、`read` 尾随 ACK、`getstorageinfo` 支持情况。
3. MTK SEND_DA 状态字（部分 BROM 回 checksum 而非 0x0000）与 DA 私有校验和封包。
4. MTK preloader 0x24 字段含义、`0x5F534655`（"UFS_"）等定制头。
5. SPRD FDL1/FDL2 状态字节语义、READ_FLASH 数据阶段是否分帧、真实 PAC 结构。
6. RockUSB DEVICE_INFO 结构、SET_RESET_FLAG 参数、LBA 参数布局、PID→型号映射。
7. FEL 响应长度（4/8 字节）、部分 SoC 的 FEL_VER 结构。
8. WinUSB 枚举补充 bus/address（可从 device instance id 解析）。
