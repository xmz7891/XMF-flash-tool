# 内置官方厂商工具

XMF 随包附带三套厂商官方刷机工具，位于 `data/thirdparty/`。它们既可以作为独立后端启动，
也为主程序提供加载器、DA 文件与驱动包。**主程序的「内置模式」工作台仍然是单窗口完成流程的
推荐方式**，官方工具用于兼容与对照。

## 目录结构

```
data/
├── thirdparty/
│   ├── miflash/                 # 小米 MiFlash（含高通/MTK 官方组件）
│   │   ├── XiaoMiFlash.exe
│   │   ├── QSaharaServer.exe    # 高通 Sahara 引导上传（命令行）
│   │   ├── fh_loader.exe        # 高通 Firehose 分区读写（命令行）
│   │   ├── da/
│   │   │   ├── MTK_AllInOne_DA.bin
│   │   │   └── MTK_AllInOne_DA_mt6873.bin
│   │   ├── SP_Download_tool/    # MTK SP 下载工具
│   │   └── Source/ThirdParty/   # Google/Qualcomm/Nvidia 驱动与 fastboot
│   ├── mtk/SP_Flash_Tool_V5.2404_Win/
│   │   ├── flash_tool.exe       # 联发科官方刷机工具
│   │   ├── MTK_AllInOne_DA.bin
│   │   ├── DA_SWSEC.bin
│   │   └── console_mode.xsd     # 控制台模式配置结构
│   └── sprd/win-x86/
│       ├── SPRDClientCore.exe   # 紫光展锐官方刷机工具
│       └── 更新日志.txt
├── drivers/
│   ├── google/     android_winusb.inf（ADB/Fastboot 驱动，小米签名）
│   ├── qualcomm/   qcser.inf（QDLoader 9008 驱动）
│   └── nvidia/     NvidiaUsb.inf
├── packets/        GeekFlashPacket 深度刷机引导包（用户提供）
└── platform-tools/ adb 37.0.1 / fastboot 37.0.1
```

## 主程序如何使用这些文件

| 用途 | 实现 |
| --- | --- |
| 「官方工具」页面启动/打开目录 | `src/app/OfficialTools.cpp`（自动检测文件存在，`QProcess::startDetached` 启动） |
| 高通 EDL 官方后端 | `src/flows` 的 OfficialQcomFlow：`QSaharaServer.exe -s 13:COMx -p <prog>` → `fh_loader.exe --sendxml=...` |
| MTK 引导文件来源 | `da/MTK_AllInOne_DA*.bin` 可作为 DA 备选（优先使用引导包内机型 DA） |
| 驱动一键安装 | `src/device/DriverManager` 递归扫描 `data/drivers/**.inf`，调用 `pnputil /add-driver /install`（需管理员） |
| 内置模式对照 | 五个工作台复刻官方布局与选项，但使用 XMF 自己的协议实现，不依赖官方 GUI |

## 合规与许可

* 官方工具版权归小米 / MediaTek / Unisoc 所有，仅按用户提供的方式随包附带，作为兼容后端使用；
  发布或再分发前请遵守各厂商的许可条款。
* 驱动包（`qcser.inf`、`android_winusb.inf` 等）同样来自上述官方工具包，安装需要管理员权限。
* XMF 自身的协议实现与界面不包含上述工具的代码；「官方工具」页面只是启动/定位文件的封装。
