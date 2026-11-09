# GeekFlashPacket 深度刷机引导包格式

引导包用于给刷机工具提供「机型 → 引导文件 + 加载地址」的映射，由用户自行导入，
与主程序解耦。目录：

```
data/packets/
├── package_qualcomm_edl-master.zip     # 直接放 zip
├── package_mtk_edl-master.zip
└── package_sprd_u2s/                  # 或者解压后的目录（需含 Config.xml）
    ├── Config.xml
    ├── DA/
    └── PreLoader/
```

程序启动时扫描 `data/packets`、`%APPDATA%/XMF/XMF Flash Tool/packets` 以及环境变量
`XMF_PACKET_DIR` 指向的目录。zip 与目录可以混用，同名包会去重。

## Config.xml

```xml
<?xml version="1.0"?>
<GeekFlashPacket>
  <PacketInfo Publisher="刷机匣" PublishDate="1759235429360"
              PublishVersion="1.0.0" Tip="无" Type="MTK" />

  <Brand Name="OPPO一加真我">
    <Series Name="通用">
      <Model Name="MT6895" Exploit="None"
             DA1Address="2097152"  DA1="/DA/OPLUS/MT6895_DA1.bin"
             DA2Address="1073741824" DA2="/DA/OPLUS/MT6895_DA2.bin"
             Preloader="" Description="天玑 8000/8100/8100-MAX" />
    </Series>
  </Brand>
</GeekFlashPacket>
```

字段说明：

| 元素/属性 | 说明 |
| --- | --- |
| `PacketInfo/@Type` | `MTK` / `QC` / `SPRD`，决定工作台归类 |
| `PacketInfo/@PublishDate` | UTC 毫秒时间戳 |
| `Model/@Name` | 机型/芯片组名称，工作台下拉框显示 |
| `Model/@Exploit` | MTK 漏洞类型：`None` / `Auto` / `Kamakiri2` / `CMD88` / `Cert` |
| `Model/@DA1Address`、`@DA1` | MTK：DA1 加载地址（十进制）与包内路径 |
| `Model/@DA2Address`、`@DA2` | MTK：DA2 加载地址与包内路径 |
| `Model/@DAPayloadAddress` | 部分 MTK 机型需要的 payload 地址 |
| `Model/@Preloader` | Preloader 文件路径，空表示自动导出 |
| `Model/@FDL1Addr`、`@FDL1`、`@FDL2Addr`、`@FDL2` | SPRD：FDL 加载地址与路径 |
| `Model/@FDL1_NoVerify`、`@FDL2_Super` | SPRD：解锁 BL 用变体（可选） |
| `Model/@PayloadAddr` | SPRD：Payload 执行地址 |
| 其他属性 | 原样保留，通过 `QVariantMap` 暴露给 UI |

包内路径以包根目录为基准，可省略开头的 `/`。zip 包中的路径会自动带上归档内的一级目录
前缀（例如 `package_mtk_edl-master/DA/...`）。

## 程序接口

```cpp
auto& store = gf::packets::PacketStore::instance();
store.refresh();                                  // 重新扫描
QVariantList packets = store.packets();           // UI 卡片
QVariantList models  = store.models(packetId);    // 机型列表（含全部属性）
QVariantMap  model   = store.findModel(packetId, "MT6895");
QString daPath = store.modelResource(packetId, "MT6895", "DA1");
// zip 包会按需解压到 %APPDATA%/XMF/XMF Flash Tool/cache/packets/<包>/...
```

对应工具：`packets.scan`（扫描统计）、`packets.export`（导出机型 CSV）。
