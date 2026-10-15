# USBCANFD 调试器 — UI 重构计划（参考 v2.0 设计稿）

> 参考文件：`/home/my/Documents/USB-CAN Pro 上位机调试工具 v2.0.html`
> 原则：参考其视觉与信息架构，但**按本项目实际功能裁剪落地**。

## 1. 目标
在**不改动 CAN/SDK 收发与线程模型**的前提下，把现有 QML 界面重构为参考稿的深色专业布局，并按本项目实际功能裁剪/落位。标题栏采用**无边框自绘**（后续迭代落地，见 §9 说明与 `README.md` 的「无边框标题栏的拖动/缩放」），应用内顶部工具条与标题栏合并为一行。

## 2. 设计系统（重写 `Theme.qml`）
- 配色：`bg #090d16` / `surface #0f172a` / `card #131d31` / `border #1e293b` / `borderLight #2a3b53` / `accent #38bdf8` / `rx #10b981` / `tx #38bdf8` / `warn #f59e0b` / `error #ef4444`
- 统一约定：**接收=emerald，发送=sky，错误=danger**（参考稿表格与卡片配色不一致，这里统一）
- 字体：`monoFamily` = JetBrains Mono / Fira Code / Roboto Mono / monospace，`sansFamily` = Inter / PingFang SC / Microsoft YaHei；字体未安装时回退系统等宽
- 尺寸：`radius 8`、`rowHeight 24`、`fontSize 12`、间距梯度、`shadowColor`
- 保留现有 `property var theme` 注入模式，兼容 QML 测试

## 3. 目标布局
```
┌─ 顶部工具条: [CAN] USBCANFD 调试器  ·  ● 设备已连接 SN/固件  ─ 帮助/设置 ┐
├──────────┬─────────────────────────────────────────────┬───────────────┤
│ 通道配置  │ 实时接收报文                                │ 统计与总线负载 │
│ (264)    │  [暂停][清空][保存日志] 自动滚动 [搜索][滤波▾]│  RX/TX/ERR/状态│
│ 设备信息  │  ▾ 滤波折叠区(软件ID/类型/硬件)             │  负载仪表      │
│ CAN1卡片  │  ┌序号 时间 通道 方向 ID 类型 格式 DLC 数据┐ │  负载趋势图    │
│ CAN2卡片  │  │ ...                                     │ │  错误计数/清零 │
│ 合并开关  │  └────────────────────────────────────────┘ │  报文 Top-N    │
│ 启停接收  │  状态条: 速率/缓冲/时间格式/监听中           │               │
├──────────┴─────────────────────────────────────────────┴───────────────┤
│ 发送工作区 [单帧发送|周期发送|记录/回放]  (高约 230)                     │
└─────────────────────────────────────────────────────────────────────────┘
```
- 左栏通高；右栏统计仅占上部；底部发送区横跨中+右（与参考稿一致）
- 通道启停按确认方案：**两通道一键启停 + 合并接收开关**（CAN1/CAN2 卡片仅配置与状态显示，不做独立启停）

## 4. 文件改动清单
**重写：**
- `src/qml/Theme.qml` — 新色板/字体/尺寸令牌
- `src/qml/Main.qml` — 新外壳（工具条 + 三栏 + 底部发送区）
- `src/qml/DevicePanel.qml` — 设备信息 + CAN1/CAN2 配置卡片 + 合并开关 + 一键启停 + 状态提示
- `src/qml/ReceiveView.qml` — 工具栏 + 可折叠滤波区 + 序号列表 + 底部状态条
- `src/qml/StatisticsPanel.qml` — 指标卡 + 负载仪表 + 趋势图(`RealtimePlot`) + 错误计数 + Top-N
- `src/qml/SendPanel.qml` — 紧凑横向单帧发送（含"高级"折叠：数据生成/多帧列表）
- `src/qml/PeriodicPanel.qml` — 紧凑定时发送表格
- `src/qml/RecordPanel.qml` — 紧凑记录/回放
- `resources.qrc` — 注册新增组件与图标

**新增：**
- `src/qml/SendWorkspace.qml` — 底部页签容器（单帧/周期/记录回放）
- `src/qml/components/`：`AppCard.qml`、`AppButton.qml`、`AppComboBox.qml`、`AppTextField.qml`、`AppCheckBox.qml`、`AppRadioButton.qml`、`MetricCard.qml`、`StatusDot.qml`、`SectionHeader.qml`、`ChannelBadge.qml`、`ToolbarButton.qml`、`DataByteField.qml`
- `src/qml/icons/*.svg`（约 12 个：pause/play/clear/save/search/filter/send/refresh/plug/check/warning/link）

## 5. C++ 小幅改动（已确认允许）
1. `model/FrameListModel.{h,cpp}`：新增 `SequenceRole`("sequence")，模型内单调递增序号（与帧同生共灭），用于"序号 #"列。`roleNames()` 由 13 → 14。
2. `core/CanFilter.{h,cpp}`：新增 `Q_PROPERTY(QString searchText …)`，在 `matchesConfig()`/`matches()` 中做文本匹配（ID 十六进制子串 + 数据 Hex 子串，忽略空格与大小写）。代理模型自动生效。
3. 说明：参考稿的 TEC/REC **SDK 不提供**（`ZCAN_ERR_MSG` 仅 hdr+dat），改用现有 `errorFrames`/`busErrorCount` 作为"错误计数"卡片，`清零` 接 `statistics.reset()`。通道显示 `CAN1/CAN2` 在 QML 侧由 `channel+1` 得出，无需 C++。

## 6. 实施步骤
1. **设计底座**：重写 `Theme.qml`；新建 `components/*` 与图标；更新 `resources.qrc`；保证空壳可编译。
2. **外壳**：重写 `Main.qml`（工具条 + 布局），旧面板临时占位，编译通过。
3. **C++**：加 `SequenceRole` 与 `searchText`；更新相关单测。
4. **左栏**：重写 `DevicePanel.qml`（含 CAN1/CAN2 卡片、合并开关、一键启停）。
5. **中栏**：重写 `ReceiveView.qml`（工具栏/搜索/可折叠滤波/序号表/状态条），保持 `rx.*` 绑定契约。
6. **右栏**：重写 `StatisticsPanel.qml`（卡片/仪表/趋势/Top-N/错误清零）。
7. **底部**：新增 `SendWorkspace.qml`，改写 `SendPanel/PeriodicPanel/RecordPanel` 为紧凑形态并接入页签。
8. **打磨**：滚动条细样式、控件焦点态、空列表占位、离线态提示。
9. **测试与验收**（见 §7）。

## 7. 测试与验收
- 构建：`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
- 测试：`ctest --test-dir build --output-on-failure`
  - 更新 `tests/test_framelist_roles.cpp`（角色数 13→14，新增 sequence 断言）
  - 更新 `tests/test_canfilter.cpp` / `test_filter_proxy.cpp`（`searchText` 用例）
  - 更新 `tests/qml/tst_panels.qml`（新增 `SendWorkspace` 与组件的编译/实例化校验；面板仍传入 `theme`）
- UI 冒烟：`QT_QPA_PLATFORM=offscreen ./build/CanDebugger` 无 QML 报错；可选 `xvfb-run -a ./build/CanDebugger` 截图核对
- 功能回归：离线可正常渲染；设备在线时暂停/清空/滤波/搜索/发送/记录/回放行为与旧版一致

## 8. 风险与注意
- 底部发送区高度有限：原 `SendPanel` 偏长，需"基础表单常显 + 高级折叠"避免挤压；多帧列表放入折叠/内部滚动
- QML 组件作用域：沿用 `property var theme` 逐层传递，避免引入 qmldir 单例带来的测试/资源路径风险
- 图标以内联 SVG 入 qrc，避免字体图标依赖
- 不改变接收路径逐帧信号与合并刷新机制，避免性能回退
- `FilterCheck`/`HeaderCell` 等内联 component 随 `ReceiveView` 重写一并迁移

## 9. 不做的事
- ~~不做无边框自绘标题栏~~ → **已改为做**（2026-10-15 迭代）：去掉系统标题栏，标题栏与工具条
  合并为一行，窗口按钮自绘，拖动/缩放按后端分流（Wayland 交给合成器，X11 自算；原因见
  `README.md`「无边框标题栏的拖动/缩放」）
- 不做按通道独立启停（采用一键启停两通道 + 合并接收）
- 不改动收发/线程/记录格式等核心逻辑
