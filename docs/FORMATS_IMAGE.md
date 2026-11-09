# XMF Flash Tool — 镜像格式说明（image 模块）

本文记录 `src/image/` 实现的固件/镜像格式要点、工具清单与已知限制。实现均为纯 C++20/Qt Core，
不依赖平台 SDK、不联网；所有解析失败都会抛出带中文说明的 `std::runtime_error`。

## 1. 工具与后端对照

| 工具 id | 说明 | 后端实现 |
| --- | --- | --- |
| `image.payload.info` / `unpack` / `pack` | payload.bin 解析、导出、打包（REPLACE） | `Payload.cpp` + 自实现 protobuf（`Proto.cpp`） |
| `image.boot.info` / `unpack` / `repack` | Android boot v0-v4 | `BootImage.cpp` |
| `image.init_boot.*` / `image.vendor_boot.*` | init_boot(v4)、vendor_boot(v3/v4) | `BootImage.cpp` |
| `image.cpio.unpack` / `pack`、`image.ramdisk.modify` | cpio newc/crc，ramdisk 增删改权限 | `Cpio.cpp` |
| `image.super.info` / `split` / `merge` / `resize` | lp 动态分区元数据、拆分、合并、扩容 | `SuperImage.cpp` |
| `image.sparse.info` / `unpack` / `pack` | Android sparse（raw/fill/dontcare/crc32） | `Sparse.cpp` |
| `image.vbmeta.info` / `disable`、`image.vbmeta.verify` | AVB 头、flags、hashtree 校验 | `Vbmeta.cpp` |
| `image.gpt.info` / `backup` / `restore` / `repair` / `extract` | GPT/MBR 解析、备份恢复、CRC 修复、按名提取 | `Gpt.cpp` |
| `image.convert.img2bin` / `bin2img` | raw 与 sparse 解包/复制 | `ImageToolsConvert.cpp` |
| `image.convert.img2lzo` / `lzo2img` | lzop 容器 + LZO1X | `Compression.cpp` |
| `image.convert.img2gz` / `gz2img` | gzip/deflate（CRC32 校验） | `Compression.cpp` |
| `image.convert.number` | hex/十进制/单位换算 | `ImageToolsConvert.cpp` |
| `image.convert.detect` | magic 类型识别 | `ImageToolsConvert.cpp` |
| `image.scan.blank` | 空白块扫描 | `ImageToolsConvert.cpp` |
| `image.zip.info` / `extract` / `verify` | zip 卡刷包、updater-script、CRC 校验 | `ZipPackage.cpp` |
| `image.elf.info` / `symbols` | ELF32/64、段、节、符号表 | `ElfFormat.cpp` |
| `image.hash.file`、`image.partition.extract` | 哈希批量校验、按偏移提取 | `ImageToolsFirmware.cpp` |
| `image.convert.batchScan` | 目录镜像批量扫描/导出 + 清单 | `ImageToolsConvert.cpp` |
| `image.bootanimation.extract` | bootanimation.zip 帧提取 | `ImageToolsConvert.cpp` |

计划中（`addPlanned`，UI 显示「开发中」）：`image.bootlogo.extract`、`image.payload.applyDiff`、
`image.convert.zstd`。

## 2. 格式要点

### payload.bin（ChromeOS update_engine）
- 头：`"CrAU"` + `file_format_version`(u64 BE，2) + `manifest_size`(u64 BE) + `metadata_signature_size`(u32 BE，v2+)。
- manifest 为 protobuf：`block_size=3`、`partitions=13`、`signatures_offset=4`、`signatures_size=5`。
- `PartitionUpdate`：`name=1`、`operations=5`、`new_partition_info=6`、`filesystem_type=4`。
- `InstallOperation`：`type=1`、`data_offset=2`、`data_length=3`、`src_extents=4`、`dst_extents=6`、
  `dst_length=7`、`data_sha256_hash=8`。`Extent`：`start_block=1`、`num_blocks=2`。
- 数据偏移是**文件绝对偏移**；`REPLACE` 数据长度可小于目标区域尾部（最后一个块按镜像实际大小截断）。
- 本工具支持 `REPLACE/ZERO/DISCARD`；`REPLACE_BZ/REPLACE_XZ` 以及所有差分操作会明确报错
  （构建环境没有 bz2/lzma 库，也不做增量算法）。打包只生成 `REPLACE` 且不写签名
  （`metadata_signature_size=0`，`signatures_* = 0`），因此不是 OTA 可校验的官方包，仅供
  fastboot 侧提取/本地回刷使用。

### boot / init_boot / vendor_boot
- boot 头 magic `ANDROID!`，`header_version` 位于偏移 40。
  v0-v2：页对齐（`page_size` 在偏移 36），布局 header/kernel/ramdisk/second/[recovery_dtbo]/[dtb]；
  header id = SHA1(kernel+size, ramdisk+size, second+size) 前 8 字节，写在偏移 576。
  v3/v4：页大小固定 4096，头部 1580/1584 字节，布局 header/kernel/ramdisk/[boot signature]。
- init_boot 使用 boot v4 结构：`kernel_size=0`，只有 ramdisk；解析时按此识别。
- vendor_boot magic `VNDRBOOT`，头部 2112（v3）/2128（v4）字节；布局
  header/vendor_ramdisk/dtb/[ramdisk 表]/[bootconfig]；v4 的 ramdisk 表项 108 字节
  （size、offset、type、name[32]、board_id[16]），据此拆分 ramdisk 分片。
- ramdisk 常见格式：cpio newc，外层 gzip 或 lzop（本工具可解压 gzip/lzop；lz4/xz/bz2 会报错）。
- 重打包时若提供的 ramdisk 是 cpio，会自动 gzip；提供目录则自动打包 cpio + gzip。
- boot v4 的 boot signature 原样保留（不生成/不校验签名）。

### cpio newc
- 记录头 110 字节：`070701`（newc）/`070702`（crc）+ 13 个 8 字符十六进制字段；
  记录名与数据均按 4 字节对齐；以 `TRAILER!!!` 结束。
- 解包时拒绝 `..` 与绝对路径；符号链接在 Windows 上写为 `<name>.symlink.txt`（含目标路径）。
- SELinux 上下文无法存入 cpio 扩展属性，`image.ramdisk.modify` 的 selinux 选项会写入/追加
  `file_contexts` 条目，属于近似处理。

### super.img（liblp）
- 前 4096 字节：geometry（magic `gDla`=0x616C4467，struct_size=24，crc32，metadata_max_size，
  slot_count，logical_block_size=4096）。
- 之后每个槽 `metadata_max_size` 字节：header（magic `LP0A`=0x414C5030，major 10，header_size 216，
  header_crc32，tables_size，tables_crc32，4 个表描述符，flags）+ 表
  （partition 52 / extent 24 / group 48 / block_device 64 字节，512 字节扇区）。
- 解析容错：extent 表项同时接受紧凑 24 字节与带 padding 的 32 字节布局；元数据槽依次尝试，
  取第一个 CRC 通过的槽；校验失败只告警并仍按槽 0 解析。
- 合并布局：分区从第 2048 扇区（1 MiB）开始，按 1 MiB 对齐；单块设备 `super`；
  默认 group `default`（不限制大小）；默认写 2 个元数据槽（可 1~4）。
- 写入的 geometry/header/tables CRC 均按 liblp 规则计算（checksum 字段自身置 0 后 CRC32）。
- 限制：不支持多块设备 super、不支持 `attributes`（readonly 等）编辑、不支持 group 大小限制；
  扩容只能增加尾部空闲空间，缩容不能小于分区数据结束位置。

### Android sparse
- 头 28 字节：magic 0xED26FF3A、major/minor、file_hdr_sz=28、chunk_hdr_sz=12、blk_sz、
  total_blks、total_chunks、image_checksum。
- chunk 12 字节头 + 数据：Raw(0xCAC1)、Fill(0xCAC2, 4 字节 pattern)、DontCare(0xCAC3)、Crc32(0xCAC4)。
- 打包为流式实现（16 MiB raw 缓冲、1024 块批处理），支持超大镜像；Crc32 chunk 只解析不输出数据。

### vbmeta / AVB
- 头 256 字节（大端）：magic `AVB0`、required major/minor(u32)、auth/aux block size、algorithm、
  hash/signature/public key/public key metadata/descriptors 的 offset+size、rollback index、flags、
  rollback index location、release string(48)。
- 描述符：`tag`(u64，取高 32 位) + `num_bytes_following`(u64) + body（按 8 字节对齐）。
  hashtree body 采用紧凑编码 184 字节（version、image_size、tree_offset/tree_size、
  data/hash block size、fec 参数、hash_algorithm(32)、root_digest(64)、salt(32)）。
- flags：bit0 `HASHTREE_DISABLED`，bit1 `VERIFICATION_DISABLED`。
- 关闭校验：仅改 flags 会破坏原签名；勾选「清零签名块」会清空 authentication block，
  两种情况都需要重新签名（工具会提示）。
- DM-verity 校验：按 `salt || block` 的 SHA256 逐层构建哈希树，最后对顶层块求根哈希并与
  `root_digest` 比较；支持 sha1/sha256/sha512、自定义 salt 与块大小。

### GPT / MBR
- 保护性 MBR（类型 0xEE）位于 LBA0；主头 LBA1，分区表 LBA2 起；备份表与备份头位于磁盘末尾。
- 头 92 字节：`EFI PART`、revision、header_size、header CRC32、my/alternate LBA、可用区间、
  disk GUID、entries LBA/数量/项大小/CRC32。
- 分区项 128 字节：类型/唯一 GUID、首末 LBA、属性、UTF-16LE 名称（72 字节）。
- 扇区大小可选 512/4096；修复会重算两个 CRC、重建主/备副本、补齐保护性 MBR；
  备份文件格式为「MBR + 主头 + 分区表」，恢复时按目标磁盘大小重建备份副本。
- 提取分区采用流式读取，不会把整盘载入内存。

### 压缩与转换
- **gzip**：自实现 inflate（stored/fixed/dynamic Huffman），压缩端借用 Qt 的 zlib 封装
  （`qCompress`）剥离包装后写入 gzip 容器，因此无需链接 zlib 库。
- **LZO1X / lzop**：解压支持标准 LZO1X 压缩块；压缩端只输出**字面量编码**
  （格式合法但体积不减小，lzop 容器中当压缩不划算时按原样存储），仅用于兼容工具链。
- **img↔bin**：字节级转换；若输入是 sparse 镜像会自动解包为 raw。
- 空白块扫描按块读取，报告全零块数量与空白区间。

### zip 卡刷包
- 支持存储（method 0）与 deflate（method 8），不支持 ZIP64/加密/bzip2/lzma 条目。
- 逐条目校验 CRC32；updater-script 从 `META-INF/com/google/android/updater-script`
  提取并解析 `package_extract_file/dir`、`ui_print`、`mount`、`format`、`set_perm`、`assert`。
- 解压时拒绝 `..`/绝对路径条目。

### ELF
- 支持 ELF32/ELF64、大小端、程序头/节头/symtab/dynsym；符号名从关联 strtab 读取。
- `image.elf.symbols` 可过滤并导出文本；内核 ELF 常见为 AArch64 EXEC。

## 3. 已知限制
1. payload 的 `REPLACE_BZ` / `REPLACE_XZ` 与全部差分操作未实现（无 bz2/lzma 依赖）。
2. boot 签名、vbmeta 签名不生成/不验证（需要私钥与完整 AVB 工具链）。
3. 不支持 lz4 压缩的 ramdisk / 内核（会明确报错）。
4. super 不支持多块设备与 group 大小限制；合并时元数据头按 216 字节写入（liblp v10）。
5. `image.gpt.info/repair/backup/restore` 需要整盘镜像读入内存（>2GB 请改用分区提取）。
6. 符号链接在 Windows 上以文本文件形式解出。
7. 所有 UI 交互都在处理器内完成：进度 0~100、支持取消（`TaskContext::checkCanceled`）。
