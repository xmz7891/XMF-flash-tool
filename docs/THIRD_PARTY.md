# 第三方组件与许可

XMF Flash Tool 自身代码由本项目维护；随包分发的第三方组件与许可如下。
分发或商用前请自行确认各组件许可证要求。

## 运行库与基础库

| 组件 | 版本 | 许可 | 用途 |
| --- | --- | --- | --- |
| Qt | 6.8.3 | LGPL v3（动态链接） | 界面、网络、SQL、SVG |
| MinGW-w64 运行库 | GCC 13.1 | GPL + Runtime Exception | 编译器运行时 DLL |
| miniz | 内置 | MIT | ZIP 解压（引导包 / 卡刷包） |
| zstd | 1.5.6 | BSD-3-Clause | `.zst` 镜像解压（`data/thirdparty/oss/zstd`） |

## Android 工具

| 组件 | 版本 | 许可 | 用途 |
| --- | --- | --- | --- |
| Android Platform-Tools（adb / fastboot） | 37.0.1 | Apache-2.0 | 设备通信 |
| scrcpy | v5.0.1（win64） | Apache-2.0 | 屏幕镜像（`data/thirdparty/oss/scrcpy`） |
| libusb-1.0.dll（scrcpy 附带） | 1.0.x | LGPL-2.1 | 屏幕镜像依赖 |
| payload-dumper-go | 最新随包版本 | Apache-2.0 | Payload 解包（`data/thirdparty/oss`） |

## 界面资源

| 资源 | 许可 | 用途 |
| --- | --- | --- |
| Lucide Icons | MIT | 界面图标（`resources/icons`） |
| Inter 字体 | SIL OFL 1.1 | 数字/拉丁字符（`resources/fonts`） |

## 官方厂商工具（专有软件）

以下工具来自各厂商官方发布，**版权归各厂商所有**，仅作为兼容后端随包附带，
不修改其代码、不绕过其校验。使用者需遵守各厂商的最终用户许可协议：

| 工具 | 来源 | 用途 |
| --- | --- | --- |
| 小米 MiFlash | 小米官方 | 内嵌运行 / QSaharaServer / fh_loader 后端 |
| SP Flash Tool | MediaTek 官方 | 内嵌运行 / MTK 刷写参考 |
| Unisoc Research Download | 紫光展锐官方 | 内嵌运行 / PAC 刷写参考 |

## 深度刷机引导包

`data/packets` 下的三个 高通 / 联发科 / 紫光展锐深度刷机引导包由用户提供，格式与内容版权归其作者。

## 打包工具

发布 ZIP 使用 7-Zip（LGPL）生成，7-Zip 本身不随包分发。
