# XMF Flash Tool — 架构说明

## 分层

```
┌──────────────────────────────────────────────────────────────┐
│ QML 界面 (qml/)                                              │
│  Main.qml 侧栏 / StackView 页面 / 玻璃组件 / ToolSheet 表单   │
└───────────────▲──────────────────────────────────────────────┘
                │ App 单例 (QML 可调用)
┌───────────────┴──────────────────────────────────────────────┐
│ src/app    AppController  TaskManager  Settings  Logger        │
│            ToolRegistry（工具目录 + handler 分发）              │
└───────────────▲──────────────────────────────────────────────┘
                │ C++ 接口
┌───────────────┴──────────────────────────────────────────────┐
│ 后端模块（互相解耦，全部依赖 gf_core）                          │
│  image  packets  device  usb  qcom  mtk  sprd  rk  aw         │
└──────────────────────────────────────────────────────────────┘
```

* **QML 不直接调用后端细节**：所有耗时操作都通过 `App.runTool(id, options)` 进入
  `TaskManager`，在 `QThreadPool` 中执行，进度通过信号回到 UI。
* **工具目录是数据驱动的**：`ToolRegistry` 收集各模块注册的 `ToolSpec`，QML 只负责渲染
  卡片网格与根据 `fields` 生成参数表单。新增工具无需改 UI。
* **引导包驱动台**：`src/packets` 解析 GeekFlashPacket（Config.xml + 二进制资源），为
  高通/MTK/展锐工作台提供机型、DA/FDL/Preloader 的选择与加载地址。
* **协议层只依赖 `gf::usb::IUsbTransport`**：真实实现走 WinUSB；单元测试使用
  `MockTransport` 注入脚本化响应，因此协议解析可在无硬件环境中验证。

## 线程模型

| 线程 | 职责 |
| --- | --- |
| GUI 线程 | QML、模型更新、`TaskManager` 信号派发 |
| QThreadPool 线程 | 每个 Tool handler / 刷写 Job 的执行体 |
| 任务取消 | `TaskContext::checkCanceled()` 抛异常，handler 自行选择安全退出点 |

`TaskManager` 维护活动任务表（QML `App.activeTasks`）与最近完成列表（`App.recentTasks`）。
并发度由 `QThreadPool::globalInstance()` 决定，后续批量刷机会用独立池限流（每设备 1 个任务）。

## 数据与路径

* 可执行目录：`<app>/bin/XMF.exe`，运行时资源 `data/` 位于 exe 同级的 `data/`。
* `data/packets/`：高通 / 联发科 / 紫光展锐深度刷机引导包（zip 或解压目录）。
* 用户数据：`%APPDATA%/XMF/XMF Flash Tool/`（日志、缓存、下载）。
* 日志同时写入 `logs/xmf-<时间戳>.log` 与内存模型 `LogModel`。

## 原生窗口效果

`src/app/NativeWindow.cpp`：
* Windows 11：`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW`（亚克力）+ 圆角 + 深色标题栏。
* Windows 10：回退到 `SetWindowCompositionAttribute(ACCENT_ENABLE_ACRYLICBLURBEHIND)`。
* QML 窗口 `color: transparent`，根容器圆角 16px，自绘标题栏与窗口控制按钮，
  边缘放 `startSystemResize` 热区实现无边框缩放。

## 阶段路线

| 阶段 | 内容 | 验收 |
| --- | --- | --- |
| 1 | 构建 + 界面框架 + 工具管线 | `XMF.exe` 可运行，截图检查玻璃界面，系统工具可执行 |
| 2 | image 全套工具 + packets + device 层 | 单元测试覆盖关键格式往返；`xmz_tests` 全绿 |
| 3 | 五平台协议 + 内置模式工作台 | MockTransport 协议回放测试；模式页可交互 |
| 4 | 固件检索、批量、驱动、打包 | 便携版 zip 双击可用；文档完整 |

## 测试策略

* 文件格式：用代码生成最小合法样本（payload、sparse、super、GPT、vbmeta），做
  `pack → parse → 比对` 往返测试；同一份真实样本（引导包内的 DA/Preloader、ELF）做解析冒烟。
* 协议：`MockTransport` 按协议时序返回脚本化数据包，断言请求字节序列与状态机转换。
* UI：`--screenshot` 冒烟 + 关键页面截图人工检查。
