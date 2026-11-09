# 内置模式工作台（qml/pages/modes）说明

内置模式工作台是一组**自包含**的 QML 页面：只依赖 `XMF` 模块自带的主题与组件，
分区 / 镜像表全部来自 `App.workspace.*` 的真实解析结果，未加载固件或设备时显示空状态，
不写死任何演示数据。导航由 `ModesPage.qml` 的 `openMode(string)` 信号 + `Main.qml` 的
`openModePage()` 完成（本目录不修改这两处）。

## 1. 文件清单

| 文件（QML 类型名） | 说明 |
| --- | --- |
| `ModeFrame.qml`（`ModeFrame`） | 工作台外框：标题栏（返回 + 标题 / 标签 / 右上操作）、固件加载行、内容区、底部状态栏（状态灯 + 进度 + 百分比 + 速度 + 设备状态 + 设备代号 / BL 状态 / 当前槽位） |
| `ModeCard.qml`（`ModeCard`） | 玻璃卡片：标题行（图标 / 标题 / 副标题 / 右上操作插槽）+ 内容插槽（自动填满剩余空间） |
| `ModePartitionTable.qml`（`ModePartitionTable`） | 分区 / 镜像表：空状态、表头、行悬停、勾选、每行「刷写」操作；列由 `columns` 动态描述 |
| `ModeLog.qml`（`ModeLog`） | 运行日志面板：等宽字体、级别色点、自动滚动开关、复制 / 清空；`append(level, message)` 写入 |
| `ModeCheckBox.qml`（`ModeCheckBox`） | 16px 玻璃勾选框，只发 `toggled(bool)`，不自己回写 `checked` |
| `ModeStatusLamp.qml`（`ModeStatusLamp`） | 状态灯：`idle / ready / busy / ok / fail` 五态 |
| `ModeProgressBar.qml`（`ModeProgressBar`） | 细进度条：数值动画 + `indeterminate` 循环色块 |
| `ModeTableRow.qml`（`ModeTableRow`） | 通用表格行（悬停 + 分隔线 + 整行点选），供自定义表格复用 |
| `MiflashModePage.qml`（`MiflashModePage`） | MiFlash 工作台：线刷包（tgz）解析、三种刷机模式、镜像表、进度 / 日志 |
| `SpFlashModePage.qml`（`SpFlashModePage`） | SP Flash Tool：scatter 解析、下载 / 仅选分区 / 格式化+下载 / 读回 / 内存测试、BROM 状态 |
| `SpdFlashModePage.qml`（`SpdFlashModePage`） | SPD Flash Tool：PAC 解析、FDL 状态、全部下载 / 单分区 / 备份 / 格式化、Pass / Fail |
| `RkDevToolModePage.qml`（`RkDevToolModePage`） | RKDevTool：MaskROM / Loader 状态；固件容器解析待接入，可读取 ADB 设备分区 |
| `PhoenixSuitModePage.qml`（`PhoenixSuitModePage`） | PhoenixSuit：FEL 状态；固件容器解析待接入，可读取 ADB 设备分区 |

所有类型都在 `XMF` 模块内（`qt_add_qml_module` 自动收集），页面里 `import XMF` 后按类型名直接使用；
`qml/pages/modes/` 子目录不影响类型注册。

## 2. 真实数据接入（App.workspace）

「浏览」用 `FileDialog` 选择文件后调用对应的解析接口，结果直接喂给表格；
「刷新设备」在 `App.adbDevices.length > 0` 时读取设备分区，否则只提示并保持空状态：

| 页面 | 加载接口 | 文件类型 |
| --- | --- | --- |
| MiFlash | `App.workspace.loadMiFlashPackage(path)` | 小米 / 红米 `.tgz` / `.tar.gz` |
| SP Flash | `App.workspace.loadScatter(path)` | MTK `scatter.txt` |
| SPD Flash | `App.workspace.loadPac(path)` | 展锐 `.pac` |
| RK / Phoenix | 固件容器解析后续接入（空状态提示） | — |
| 设备分区（全部页面） | `App.workspace.loadAdbPartitions("")` | `adb devices -l` + `/dev/block/by-name` |

返回值统一为：

```js
{ ok: bool, error: string, kind: string, title: string, subtitle: string,
  partitions: [ { name, sizeText, sizeBytes, address, file, region, detail, checked } ] }
```

页面处理方式（所有页面一致）：

```qml
function loadScatter(path) {
    if (path.length === 0) { scatterDialog.open(); return }
    const result = App.workspace.loadScatter(path)
    if (!result.ok) {
        App.notify("error", result.error || "scatter 解析失败")
        logBox.append("error", result.error || "scatter 解析失败")
        return
    }
    frame.fwPath = path
    frame.fwLoaded = true
    frame.statusText = "scatter 已加载 · 等待 BROM 设备"
    frame.statusKind = "ready"
    table.setPartitions(result.partitions || [])   // 表格按 checked 字段初始化勾选
    page.tableSubtitle = result.subtitle            // 卡片副标题显示 result.subtitle
    logBox.append("success", "scatter 解析完成：" + page.tableSubtitle)
}
```

* 加载失败用 `App.notify("error", result.error)` 提示，表格保持空状态。
* 勾选状态保存在 `ModePartitionTable` 内部数据里（`setPartitions` 时按 `checked` 初始化），
  切换行 / 全选只改内部状态，刷新后按新结果重建。
* RK / Phoenix 的固件容器解析尚未接入：选择文件后只记录路径并提示
  「固件容器解析将在后续接入」，不显示任何假分区。

## 3. 视觉约定（亮色 / 深色双主题）

* **只用中性色**：所有颜色取自 `Theme` token，禁止页面内硬编码十六进制颜色。
  - 文字：`Theme.text` / `Theme.textSub` / `Theme.textMuted`
  - 面板：`GlassSurface`（已适配亮暗）、`Theme.glass` / `Theme.glassStrong` / `Theme.glassHover`
  - 描边：`Theme.border` / `Theme.borderStrong`；分隔线：`Theme.divider`
  - 强调：`Theme.accent`（深色=白、浅色=近黑）与 `Theme.accentSoft`
  - 语义色：`Theme.success` / `Theme.warning` / `Theme.danger` **仅**用于状态灯、状态徽标与 Pass/Fail
* 形状与动效保持克制：1px 描边、`Theme.r18` 卡片 / `Theme.r10` 小控件、行高 32px；
  不使用渐变发光、光晕、彩色强调块。
* 模式标签用具体模式名：`FASTBOOT` / `BROM` / `FDL` / `ROCKUSB` / `FEL`。
* 参考真实刷机工具（KM Flash Tool 风格）：分区表列 `LUN / 名称 / 大小 / 镜像文件 / 操作`，
  操作列每行提供「刷写」；底部状态栏分两行——进度 / 速度 / 设备连接，以及
  设备代号 / BL 状态 / 当前槽位。

## 4. ModeFrame 接口

```qml
ModeFrame {
    title: "SP Flash Tool"        // 标题
    subtitle: "联发科 scatter…"    // 副标题
    icon: "microchip"             // lucide 图标名（resources/icons/white）
    tag: "BROM"                   // 标题右侧小标签（可空）
    accentColor: Theme.accent     // 强调色（保持默认即可）

    firmwareLabel: "Scatter"      // 固件栏左侧标签
    firmwarePlaceholder: "…"      // 路径输入占位
    fwPath: "D:\\…\\scatter.txt"  // 固件路径（由页面/文件对话框写入）
    fwLoaded: true                // 显示「已加载」徽标

    progress: 0                   // 0..100，底部进度条
    indeterminate: false          // 不确定进度色块
    statusText: "就绪 · 等待设备"   // 状态灯旁的文字
    statusKind: "idle"            // idle / ready / busy / ok / fail
    deviceText: "未检测到设备"
    deviceKind: "idle"
    speedText: "—"                // 进度条右侧速度
    deviceCode: "—"               // 状态栏：设备代号
    bootloaderText: "未知"         // 状态栏：BL 状态
    slotText: "—"                 // 状态栏：当前槽位

    onBackRequested: page.backRequested()   // 返回
    onBrowseRequested: fileDialog.open()    // 浏览
    onLoadRequested: page.reload()          // 加载 / 重新加载
    onReloadRequested: page.scanDevice()    // 底部刷新按钮

    headerActions: [              // 标题栏右上操作（list 赋值给 Row.data）
        AppButton { text: "刷新设备"; icon: "refresh-cw"; kind: 1; small: true; onClicked: … },
        AppButton { text: "下载"; icon: "download"; kind: 0; small: true; onClicked: … }
    ]

    // default 属性：内容区（Item，自动填满标题/固件/状态栏之间）
    RowLayout { anchors.fill: parent; … }
}
```

## 5. ModePartitionTable 接口

```qml
ModePartitionTable {
    id: table
    anchors.fill: parent
    checkable: true
    columns: [
        { label: "LUN",      role: "lun",     width: 78 },
        { label: "名称",     role: "name",    flex: true },
        { label: "大小",     role: "size",    width: 68, align: "right", mono: true },
        { label: "镜像文件", role: "file",    width: 150, mono: true },
        { label: "操作",     role: "action",  width: 44, align: "right" }
    ]
    rowActionText: "刷写"
    emptyIcon: "file-archive"
    emptyTitle: "尚未加载 scatter 分区表"
    emptyText: "点击「浏览」选择 MTK scatter.txt……"
    emptyActionText: "选择 scatter.txt"
    onEmptyAction: scatterDialog.open()
    onRowAction: function (index) { page.runAction("刷写 " + table.rowAt(index).name) }
}
```

* `role` 支持：`name` / `size` / `address` / `file` / `detail` / `lun` / `stateText` / `checkState` / `action`。
* `columns` 中 `flex: true` 的列占满剩余宽度；`align: "right"` 右对齐；`mono: true` 用等宽字体。
* 勾选：`setPartitions(list)` 按 `checked` 初始化，`selectedCount` / `allSelected` 只读可用，
  外部可通过 `table.setAll(true)`；整行点击与勾选框都走同一状态。
* 空状态：`count === 0` 时显示 `EmptyState`，动作按钮触发 `emptyAction()`。

## 6. 组件使用示例

```qml
// 卡片
ModeCard {
    Layout.fillWidth: true
    Layout.fillHeight: true
    title: "Scatter 分区表"
    subtitle: page.tableSubtitle.length > 0 ? page.tableSubtitle : "尚未加载 scatter.txt"
    icon: "list-checks"
    actions: [ ModeCheckBox { visible: table.count > 0; checked: table.allSelected; onToggled: … } ]
    ModePartitionTable { id: table; anchors.fill: parent; … }
}

// 日志
ModeLog {
    id: logBox
    Layout.fillWidth: true
    Layout.fillHeight: true
    seedLines: [ { level: "info", message: "工作台已就绪，等待加载固件" } ]
}
// 运行期写入
logBox.append("success", "scatter 解析完成：12 个分区")
```

`level` 取值：`info` / `success` / `warning` / `error` / `debug`。

## 7. QML 注意事项（本项目踩过的坑）

* `font.pixelSize` 必须是整数（不要写 11.5）。
* 继承 `GlassSurface` 的组件（`ModeCard` / `ModeLog`）不要重复声明父类型已有的属性
  （`color` / `radius` / `border` / `accentColor` / `interactive` 等），需要时直接赋值。
* 图标统一用 `IconImage { name: "lucide-name"; tint: … }`，名字见 `resources/icons/white/*.svg`。
* 嵌套布局（`ColumnLayout` 作为 `RowLayout` 的子项）会被拉伸，不会被
  `Layout.preferredWidth` 约束；本目录统一用 `Item { Layout.preferredWidth: … }` 包一层。
* `ToggleSwitch` 的滑块固定为白色，深色主题下若轨道也用 `Theme.accent`（白）会糊成一片，
  页面内统一传 `accentColor: Theme.textMuted` 保证两种主题下都可辨识。
* 预览：真实数据来自 C++ 单例，脱离主程序渲染页面时可用临时模块注入 mock `App` 单例
  （仅本地截图用，不进入仓库）。

## 8. 截图

截图存于 `docs/screenshots/`：

* `modes-<page>-dark.png` / `modes-<page>-light.png`：加载真实解析结果后的表格状态
* `modes-<page>-dark-empty.png` / `modes-<page>-light-empty.png`：未加载时的空状态

阶段 3 接入刷写后端时，只需把各页面的占位 `App.notify("info", "…阶段 3 接入")`
换成真实任务调用（例如 `App.runTool(...)`），并把 `progress / speedText / statusText`
绑定到任务进度即可，视觉层无需改动。
