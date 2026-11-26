# XMF × VioletToolBox 功能对比与补齐建议

对比对象：

- VioletToolBox：约 110 项 Android 玩机功能，GPL-3.0
- XMF Flash Tool 0.4.0：注册工具 229 个，MIT

功能数量不能直接比较：XMF 的 229 个偏底层工具与镜像处理，Violet 的 110 项偏面向用户的完整工作流。

## 一、已经覆盖的能力

| Violet 模块 | XMF 对应能力 | 结论 |
| --- | --- | --- |
| 设备状态 / 重启 / 无线调试 / 槽位 | `adb.*`、`bootloader.slots`、`device.ports` | 已覆盖 |
| scrcpy 投屏 | `scrcpy` 官方工具入口 | 有入口，缺参数面板 |
| 常规镜像刷入 / 解锁回锁 / FRP | `fastboot.flash`、`bootloader.unlock`、`unlock.frp` | 已覆盖 |
| 可视刷写 ADB / Fastboot | `adb.partitions`、`fastboot.partitions`、Fastboot 工作台 | 已覆盖 |
| 小米线刷 | `miflash.inspect`、`miflash.flash` | 已覆盖 |
| EDL / 9008 | `qcom.flow.*`、`qcom.firehose.*`、`qcom.qedl.*` | 已覆盖，见下方缺口 |
| Root / 模块 / 隐藏 | `root.status`、`root.boot.patch`、`root.modules`、`root.hide` | 已覆盖 |
| 应用冻结 / 卸载 / 提取 APK | `adb.app.control`、`adb.apk.extract` | 已覆盖 |
| Payload / boot / super / vbmeta / GPT | `image.payload.*`、`image.boot.*`、`image.super.*`、`image.vbmeta.*`、`image.gpt.*` | 已覆盖 |
| OFP / OZIP 解包 | `firmware.file.unpackOfp`、`firmware.file.unpackOzip` | 已覆盖 |
| 小米固件检索 / OTA 检查 | `firmware.xiaomi.search`、`firmware.xiaomi.otaCheck` | 已覆盖 |
| 官方固件入口 | `firmware.official.lookup` | 只有入口，不是 ROM 专区 |

## 二、真正缺口

### P0：最值得补

#### 1. 欧加（OPPO / 一加 / realme）刷写工作台

Violet 有：

- 全量包：普通线刷、强力线刷、AB 通刷、FastbootD、修复 FastbootD、ARB 熔断检测、Super 修复、Payload 解包
- 售后包：散包刷写、FB / FBD 模式、全自动救砖
- 降级助手：OTA 降级、版本解析

XMF 现状：

- 只有通用 Fastboot / ADB / 镜像工具
- 没有欧加专用包结构识别、刷写顺序、ARB 检测、救砖流程
- 没有 OTA 降级入口

价值：最高，差异化最强。

#### 2. ROM 资源中心

Violet 有 OPPO / realme / 魅族 / 联想 / 小米 / Redmi 的 OTA、线刷包、boot 镜像下载和 API。

XMF 现状：

- 小米检索/下载完整
- 其他品牌只有官方入口链接
- 没有统一 OTA 索引、版本列表、断点续传

价值：高，直接提升普通用户使用频率。

#### 3. EDL 专用维护操作

Violet 有：

- 写 GPT
- 格式化指定 LUN
- 备份基带 / 指纹
- 强开 OEM
- 槽位管理
- 读取软件信息

XMF 现状：

- 有 GPT 读、分区备份、Firehose patch、rawprogram XML
- 没有“写 GPT / 格式化 LUN / 基带指纹备份 / 强开 OEM”这些一键预设
- 底层能力足够，主要缺工作流封装

价值：高，和 XMF 现有 9008 工作台最契合。

#### 4. scrcpy 参数面板

Violet 支持：

- 窗口大小
- 帧率
- 清晰度
- 标题
- 虚拟按键
- 全自动投屏

XMF 现状：只能启动 scrcpy，不能配置。

价值：中高，实现成本低。

### P1

| 缺口 | Violet 对应 | XMF 现状 | 成本 |
| --- | --- | --- | --- |
| OPLUS OCDT 生成/分析 | OCDT 分析 | 只有 `qcom.oplus.digest` 解析 | M |
| TWRP 分区读写擦 | Recovery 分区工作台 | 只有 `adb.sideload` | M |
| GKI 制作 / AVB 签名 | 脱机修补 | 有 vbmeta 校验/关闭，无签名 | L |
| 相册 / 视频 / 通讯录备份 | 备份助手 | 无 | M |
| OTA 多线程断点续传 | 断点续传 | 单线程下载为主 | M |
| 应用管理可视化 | 应用管理 | 工具有，缺列表式 UI | S |
| Slot A 精简刷写 | 可视刷写 | 无 | M |

### P2

- 小米 USB 安全设置、ADB 异常修复
- 一加 DDR 版本、基带调试端口、文件传输模式
- 多品牌刷机资源聚合
- 无线调试自动连接、快捷 CMD / 设备管理器入口

## 三、建议实现顺序

1. **欧加刷写工作台**（P0，L）
2. **ROM 资源中心**（P0，M）
3. **EDL 维护预设**（P0，M）
4. **scrcpy 参数面板**（P0，S）
5. **OPLUS OCDT**（P1，M）
6. **TWRP 分区工作台**（P1，M）

不建议照着 Violet 平铺 110 项。XMF 应该做“刷机工作流 + 底层能力”，而不是再做一个 ADB 工具箱。

## 四、许可证提醒

VioletToolBox 是 **GPL-3.0**，XMF 是 **MIT**。

- 不要直接复制 Violet 源码到 XMF
- 可以独立实现相同功能
- 如果引入其代码，XMF 需要整体改为 GPL 兼容分发

## 五、结论

XMF 的底层工具数量已经超过 Violet，真正缺的是：

- 欧加完整刷写链路
- 多品牌 ROM 资源
- EDL 一键维护
- 投屏参数化

这四项补完，XMF 的功能面基本对齐 Violet，同时保留自己的刷机深度。
