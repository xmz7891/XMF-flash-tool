# XMF Flash Tool — 固件资源数据源与接口说明

本文记录 `src/firmware/` 使用的固件数据源、公开接口验证结果、离线数据库格式、工具清单与合规声明。

## 1. 小米 / 红米固件检索

### 1.1 公开数据集（默认数据源，已验证可用）

| 数据 | URL | 说明 |
| --- | --- | --- |
| `latest.yml` | `https://raw.githubusercontent.com/XiaomiFirmwareUpdater/miui-updates-tracker/master/data/latest.yml` | 约 1.2 MB，约 3600+ 条发布记录（机型代号、版本、分支、Android 版本、包类型、日期、直链、MD5、大小） |
| `devices.yml` | `https://raw.githubusercontent.com/XiaomiFirmwareUpdater/miui-updates-tracker/master/data/devices.yml` | 代号 → 机型名称列表，用于中文/英文机型名反查代号 |

- 两者均为 GitHub 公开静态数据（XiaomiFirmwareUpdater 社区维护），**无需登录、无需 API Key**。
- 本工具首次检索时下载并缓存到 `%LOCALAPPDATA%/.../cache/firmware/`（`gf::util::cacheDir()/firmware`），
  缓存有效期 12 小时；网络不可用时自动回退到本地缓存并提示。
- `latest.yml` 为扁平 `- key: value` 列表，模块内使用最小的 YAML 子集解析器（`parseLatestYaml`），
  不引入额外依赖。

curl 验证（2026-11-08）：

```powershell
curl.exe -sS -m 20 -A "Mozilla/5.0" `
  "https://raw.githubusercontent.com/XiaomiFirmwareUpdater/miui-updates-tracker/master/data/latest.yml" `
  -o latest.yml -w "%{http_code} %{size_download}"
# -> 200 1263582
```

### 1.2 官方 MIUI OTA 接口（实验性）

- 端点：`POST https://update.miui.com/updates/miotaV3.php`
- 请求体（XML，模块内由 `buildMiuiOtaRequest` 生成）：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mi_ota_request>
  <header>
    <params>
      <locale>zh_CN</locale>
      <device>umi</device>
      <mode>miui</mode>
      <branch>stable</branch>
      <version>V12.5.1.0.RJBCNXM</version>
      <rom_version>V12.5.1.0.RJBCNXM</rom_version>
    </params>
  </header>
</mi_ota_request>
```

curl 验证结果（2026-11-08）：`GET` 返回 `405 Method Not Allowed`，带上述 XML 的 `POST` 返回
`HTTP 400`（无响应体）。该端点很可能要求小米官方 APP 的报文/签名/设备指纹，社区匿名调用不可用。
因此模块将它实现为**实验性工具**（`firmware.xiaomi.otaCheck`），失败时给出中文提示并引导用户改用
公开数据集检索；`parseMiuiOtaResponse` 对 `200` 响应中的 `<update>/<item>/<release>` 做了容错解析，
接口恢复可用时无需改代码。**不实现任何需要登录、破解或绕过验证的接口。**

### 1.3 下载与校验

- 直链域名通常为 `https://bigota.d.miui.com/...`（历史机型）或小米 CDN，均为公开 GET 直链。
- 下载器基于 `QNetworkAccessManager`：支持 `Range` 断点续传、进度回调、取消（`TaskContext::checkCanceled`），
  默认保存到 `App.settings.downloadDir`（`QSettings("XMF","XMFTool")` 的 `paths/downloads`）。
- 下载完成后按数据集提供的 MD5 校验（`verifyFileHash`，单次读文件同时算 MD5/SHA256）。

## 2. 其他品牌官方入口（离线数据库）

`data/firmware/brands.json` 收录品牌官方固件/支持页面与说明，**只提供入口与格式说明**：

| 品牌 id | 名称 | 官方入口 |
| --- | --- | --- |
| `xiaomi` | 小米 / 红米 | mi.com/service/support、miui.com、小米社区 |
| `oppo` | OPPO | oppo.com/cn/service/software、coloros.com |
| `vivo` | vivo | vivo.com.cn/service/software、OriginOS |
| `realme` | realme | realme.com/cn/support/software-update |
| `oneplus` | 一加 | oneplus.com/cn/support/software-update |
| `samsung` | 三星 | samsung.com/cn/support/download、Samsung Developers |
| `unisoc` | 紫光展锐 | unisoc.com（SoC 厂商，整机固件由品牌方提供） |
| `rockchip` | 瑞芯微 | rock-chips.com、opensource.rock-chips.com |
| `allwinner` | 全志 | allwinnertech.com、linux-sunxi.org |

JSON 结构：

```json
{
  "version": 1,
  "updated": "2026-11-08",
  "brands": [
    {
      "id": "oppo",
      "name": "OPPO",
      "subtitle": "ColorOS 官方软件升级",
      "official": [{ "title": "OPPO 官方服务 - 软件升级", "url": "https://www.oppo.com/cn/service/software/" }],
      "notes": ["..."],
      "searchHint": "..."
    }
  ]
}
```

## 3. 离线机型数据库

`data/firmware/models.json`：`codename` → 品牌/系列/市场名称列表，用于：

- `firmware.file.identify`：识别固件类型时同时匹配机型（文件名如 `miui_UMI_V14.0.4.0...zip` → `umi` → 小米10）；
- `firmware.file.matchModel`：输入文件名或机型名，返回代号/品牌/系列；
- `firmware.official.lookup`：品牌查询时补充机型代号提示。

数据结构：

```json
{
  "version": 1,
  "updated": "2026-11-08",
  "models": [
    { "codename": "umi", "brand": "xiaomi", "series": "小米10",
      "names": ["小米10", "Mi 10", "umi"] }
  ]
}
```

数据库可离线更新（直接替换 JSON 即可），解析失败会返回带文件路径的中文错误。

## 4. 固件文件工具

| 工具 id | 能力 |
| --- | --- |
| `firmware.xiaomi.search` | 按机型/代号检索公开数据集，输出版本/分支/Android/大小/MD5/直链 |
| `firmware.xiaomi.download` | 下载最新匹配完整包（断点续传、进度、取消、MD5 校验） |
| `firmware.xiaomi.otaCheck` | 官方 miotaV3 接口查询（实验性；失败给出替代方案） |
| `firmware.dataset.info` | 数据集规模/通道分布/缓存目录 |
| `firmware.official.lookup` | 其他品牌官方入口与说明（brands.json） |
| `firmware.file.identify` | 识别 zip/tgz/tar/pac/img/bin/ozip/ofp/kdz/7z/rar/gz/lz4/lzo/elf/md5 等，并给出内部格式线索与机型匹配 |
| `firmware.file.unpack` | 自动解包 zip / tar.gz / tar / PAC（zip 用 gf_image 流式条目读取；PAC 用 gf_sprd） |
| `firmware.file.hash` | 批量 SHA256/MD5 计算与校验（单次读文件同时算两种哈希） |
| `firmware.file.mergeSplit` | 合并 `.z01/.z02…+.zip` 或 `.001/.002…` 分卷 |
| `firmware.file.matchModel` | 按文件名/机型匹配离线机型库 |

计划中（`addPlanned`，UI 显示「开发中」）：`firmware.file.unpackOzip`、`firmware.file.unpackOfp`
（OPPO 加密容器；不实现破解）。

### 4.1 解包实现与限制

- **zip**：复用 `gf_image` 的 `zip::open/readEntry`（stored/deflate，CRC32 校验，路径穿越防护）。
- **tar / tar.gz**：模块内实现 tar（含 GNU 长文件名、pax path），gzip 复用 `gf_image::compress::gzipDecompress`。
  tar 解包需要整体载入内存，超过 1.5 GB 会给出「请先用系统 tar/7z」的中文提示。
- **PAC**：复用 `gf_sprd::PacInfo` 解析镜像表并提取；超过 1.5 GB 建议使用 image 模块/展锐工具。
- **OZIP / OFP**：只识别类型，不实现解密（官方加密格式）。

## 5. 合规声明

1. 本模块只访问**公开静态数据集**与**公开 HTTP 直链**，不包含任何需要登录、付费、破解或绕过授权的接口；
2. 不托管、不分发任何固件文件；下载内容版权归原厂所有，用户需自行确认设备/区域与固件匹配；
3. 刷机有风险（变砖/失去保修），工具默认只做检索、下载、哈希校验与格式解析，刷写操作由用户在对应工作台确认；
4. 官方入口仅提供链接与说明，若官方页面要求登录/授权，请按官方流程操作。

## 6. QML 侧如何调用

本模块不新增 QML，全部通过 `ToolSpec` 数据驱动（与 image 模块一致）：

- 工具自动出现在 `App.tools` 中，`category == "firmware"`，按 `group` 分组：
  `xiaomi`（检索/下载/OTA/数据集）、`brands`（官方入口）、`file`（识别/解包/哈希/合并/机型匹配）。
- 页面用 `App.runTool(id, options)` 触发，`options` 的键与 `ToolSpec.fields.key` 一一对应：
  - 检索：`{ query, branch(0-3), method(0-2), limit, refresh }`
  - 下载：`{ query, branch, method, version, outputDir, verify, resume, refresh }`
  - 官方入口：`{ brand(名称), model }`
  - 识别/解包：`{ input, output }`；哈希：`{ input: [path...], expected, algorithm }`；
    分卷合并：`{ input, output }`；机型匹配：`{ query, limit }`
- 进度/日志/取消统一走 `TaskContext`（`App.activeTasks` 自动显示）。
- 下载目录取 `App.settings.downloadDir`（与 `Settings::downloadDir` 同一 QSettings 键）。
