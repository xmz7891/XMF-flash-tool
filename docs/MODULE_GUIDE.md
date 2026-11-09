# XMF Flash Tool — 模块开发指南

本文件是多人协作的**契约**。新增功能前请先阅读，避免破坏他人代码与构建。

## 1. 目录与归属

| 目录 | 内容 | 负责人 |
| --- | --- | --- |
| `src/core` | 日志、任务引擎、工具注册表、设置、工具函数 | Lead |
| `src/app` | 应用入口、控制器、原生窗口、系统工具 | Lead |
| `qml` | 全部 QML 界面（Theme 单例、组件、页面） | Lead + UI |
| `src/image` | 固件 / 镜像文件格式工具（payload、boot、super、sparse、vbmeta、GPT…） | image-core |
| `src/packets` | 高通 / 联发科 / 紫光展锐深度刷机引导包解析与资源定位 | Lead |
| `src/usb` | USB 传输抽象（WinUSB / 测试用 Mock） | protocols |
| `src/qcom` | 高通 Sahara / Firehose | protocols |
| `src/mtk` | 联发科 BROM / DA / scatter | protocols |
| `src/sprd` | 展锐 FDL / PAC | protocols |
| `src/rk` | 瑞芯微 RockUSB | protocols |
| `src/aw` | 全志 FEL | protocols |
| `src/device` | 端口扫描、ADB/Fastboot、驱动检测、批量任务 | device |
| `tests` | 测试入口（自动收集 `src/*/tests/test_*.cpp`） | 全体 |

**规则：只修改自己负责的目录。** 不要改根 `CMakeLists.txt`、`src/app/**`、`qml/**`（除非任务明确指派）。需要新的公共能力时，先在自己模块里实现，再通知 Lead 集成。

## 2. 注册一个工具（最重要）

每个功能 = 一个 `ToolSpec` + 一个 handler。UI 会根据 spec 自动生成卡片与参数表单，**不需要写 QML**。

```cpp
#include "ToolModules.h"      // 声明 registerXxxTools()
#include "ToolRegistry.h"
#include "TaskManager.h"

namespace gf {

namespace {

void unpackPayload(TaskContext& ctx, const QVariantMap& options)
{
    const QString in  = options.value(QStringLiteral("input")).toString();
    const QString out = options.value(QStringLiteral("output")).toString();
    ctx.info(QStringLiteral("开始解析 %1").arg(in));

    // ... 真正的实现 ...
    ctx.setProgress(30);          // 0..100
    ctx.setStatus(QStringLiteral("解析 manifest"));
    ctx.checkCanceled();          // 用户取消时抛 TaskCanceled

    ctx.ok(QStringLiteral("完成，已输出到 %1").arg(out));
}

} // namespace

void registerImageTools()
{
    ToolSpec spec;
    spec.id       = QStringLiteral("image.payload.unpack");
    spec.category = QStringLiteral("image");
    spec.group    = QStringLiteral("payload");
    spec.name     = QStringLiteral("Payload 解包");
    spec.desc     = QStringLiteral("解析 payload.bin，导出全部分区镜像");
    spec.icon     = QStringLiteral("file-archive");   // lucide 图标名，见 resources/icons
    spec.keywords = {QStringLiteral("payload"), QStringLiteral("解包")};
    spec.fields   = {
        fields::path(QStringLiteral("input"),  QStringLiteral("payload.bin")),
        fields::dir (QStringLiteral("output"), QStringLiteral("输出目录")),
        fields::boolean(QStringLiteral("verify"), QStringLiteral("校验哈希"), true),
    };
    addTool(spec, unpackPayload);
}

} // namespace gf
```

要点：

* `category` 必须是 UI 分类 id：`image qcom mtk bootloader sprd rk aw root unlock adb firmware batch bypass`。
* `spec.id` 全局唯一，格式 `模块.功能[.子功能]`。
* handler 运行在**工作线程**，不能直接操作 UI；只能用 `ctx` 的 API。
* 出错用 `throw std::runtime_error("可读的中文错误")`，任务会以失败状态呈现。
* 未实现的功能用 `ToolRegistry::instance().addPlanned(spec)` 先占位（卡片显示「开发中」）。
* 图标名来自 `resources/icons/white/<name>.svg`，新增图标可跑 `python scripts/fetch_assets.py` 或加名字到 `scripts/fetch_extra_icons.py`。

## 3. TaskContext API

```cpp
int  id();                         // 任务 id
void setProgress(int percent);     // 0..100，-1 = 不确定进度
void setStatus(const QString&);    // 状态文字
void info(const QString&);         // 日志（info/success/warning/error/debug）
void ok/warn/error/debug(...);
bool isCanceled();                 // 查询取消
void checkCanceled();              // 已取消则抛 TaskContext::TaskCanceled
```

## 4. 构建与测试

每个成员使用**各自独立的构建目录**，避免 ninja 并发冲突：

```powershell
scripts\build.cmd -BuildType Debug -BuildDir build\<你的名字>
scripts\build.cmd -BuildType Debug -BuildDir build\<你的名字> -Target xmf_tests
build\<你的名字>\bin\xmf_tests.exe
```

GUI 冒烟测试（截图并退出）：

```powershell
build\<你的名字>\bin\XMF.exe --screenshot shot.png --page 2 --screenshot-delay 2000
```

## 5. 测试

把 `test_*.cpp` 放到 `src/<模块>/tests/`，模板：

```cpp
#include <QtTest>
#include "TestRegistry.h"
#include "Util.h"

class TestSparse : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip() { QCOMPARE(1 + 1, 2); }
};

#include "test_sparse.moc"
GF_TEST_CLASS(TestSparse)
```

`cmake --build ... --target xmf_tests` 后运行 `bin/xmf_tests.exe`。

## 6. 代码约定

* C++20、Qt 6.8、MinGW 13；源文件 UTF-8（无 BOM），中文注释/字符串直接用 UTF-8。
* 头文件用 `#pragma once`；include 路径以 `src` 为根时写 `"core/Log.h"`（或同模块相对路径）。
* 所有用户可见文案用中文；日志前缀用模块名，如 `GF_INFO("payload", ...)`。
* 不要引入需要联网/隐私数据的依赖；大二进制资源放 `data/` 或 `resources/`。
* 新增源文件后需更新**自己模块**的 `CMakeLists.txt`；QML 文件会被自动收集。
* 硬件协议代码必须通过 `gf::usb::IUsbTransport`，不得直接调用操作系统 USB API；测试用 Mock。

## 7. 深度刷机引导包格式（GeekFlashPacket）

`Config.xml` 根节点 `GeekFlashPacket`，含 `PacketInfo`（Type = MTK/QC/SPRD）与 `Brand → Series → Model`
结构，Model 上的属性给出 DA / Preloader / FDL 路径与加载地址。资源文件路径以包根目录为基准。
解析器在 `src/packets`，UI 为「引导包」页面。
