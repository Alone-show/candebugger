# 实施进展记录 — USBCANFD 调试器

计划来源：`../IMPLEMENTATION_PLAN.md`
工程根目录：`ui/CanDebugger/`
本次目标：**里程碑 5（步骤 10–12：记录 / 回放）**；里程碑 1–4（步骤 0–9）记录见下

环境基线（已核实）：
- Qt 5.15.17（Core/Gui/Qml/Quick/Test 的 CMake 包齐全）
- QtQuick.Controls 2 运行时可用的（`QtQuick/Controls.2`）；未安装 `Qt5QuickControls2` dev 包，故**不链接**该模块，仅 QML 运行时导入
- CMake 3.31.6、g++ 15.2.0、libusb-1.0-0-dev
- 设备 `3068:0009` 在线；设备节点 `/dev/bus/usb/...` 目前 root 可写，需 udev 规则
- `libusbcanfd.so` SONAME = `libusbcanfd.so.1.0.15`（仓库内已建同名软链）；`libusb-1.0.so.0` 由系统提供
- 波特率：**ZLG 寄存器约定** `baud = clk / ((brp+1) * (tseg1+tseg2+3))`，字段为寄存器值（有效值−1），
  `clk` 即真实时钟（V1.01=60MHz / V1.03+=80MHz）；非标准速率用「原始位定时」覆盖（见「风险 30」）

---

## ⭐ 新会话交接 — 当前状态（2026-10-15）

**本次会话主题**：把位定时换算**定论**下来 —— 上一会话的「实际时钟 = 0.8 × 名义时钟」近似
模型**已作废**，改为实测判决出的 **ZLG 寄存器约定**，使 125k/250k/500k/1M 用同一条公式精确命中。

**根因（已定论，实机判决实验）**：

```
bps       = clk / ((brp + 1) * (tseg1 + tseg2 + 3))
采样点(%) = 100 * (tseg1 + 2) / (tseg1 + tseg2 + 3)
```

- `brp / tseg1 / tseg2` 都是**寄存器值 = 有效值 − 1**；`clk` 就是**真实控制器时钟**
  （V1.01 = 60MHz、V1.03+ = 80MHz）。**不存在 0.8× 的"实际时钟"**。
- 判决实验（对端 125k，`device_bitrate_scan <clk> 0 1 64 120 3 14`，固定形状 tseg1=12/tseg2=1）：
  - 80MHz：唯一命中 `brp=39`（`80e6/(40×16) = 125000`，ACK 5/5）；60MHz：`brp=29`（ACK 5/5）。
  - 同速率多档并列 ACK（如 1M 时 60M/80M 各有数档 ACK 6/6）是 SDK 容差范围内正常现象。
- 4 速率交叉验证（对端切到该速率后 `device_bitrate_scan 80 0 1 64 120 3 14`，全程只有唯一一档命中）：

  | 速率 | brp | tseg1 | tseg2 | 校验 | 实测 |
  |---|---|---|---|---|---|
  | 125k | 39 | 12 | 1 | 80e6/(40×16) = 125000 | 有效帧 36，ACK 5/5 |
  | 250k | 19 | 12 | 1 | 80e6/(20×16) = 250000 | 有效帧 31，ACK 3/3 |
  | 500k | 9 | 12 | 1 | 80e6/(10×16) = 500000 | 有效帧 25，ACK 3/3 |
  | 1M | 4 | 12 | 1 | 80e6/(5×16) = 1000000 | 有效帧 15，ACK 3/3 |

- 旁证（ZLG 官方 demo `test.c`，clk=60MHz）：仲裁 `brp5/tseg14/3` → `6×20 = 500k`、
  数据 `brp1/tseg10/2` → `2×15 = 2M`，两条注释都精确成立。
- 旧模型为何"看起来对"：0.8× 能凑出 250k（48e6/12/16）与 500k（64e6/8/16），
  但 125k 会偏 **+6.7%**、1M 会偏 **−16.7%**（超出逐位重同步容差）→ 正是
  「本机自收自测正常、接外部节点收发失败」的原因。

**本次代码改动**：
- `src/core/BitTiming.{h,cpp}`：换成上述约定；新增 `paramsForTq()`（按总 Tq 数构造精确参数）、
  `totalTq()`；**删除** `canClockForSdkClock()`（0.8×）与 `CanClock48M/64M`；
  参数下限 brp≥1、tseg1/tseg2≥1（不使用 brp=0）；`calculate()` 的 tie-break 改为
  「总 Tq 数靠近 16 → SJW 更大」。
- `src/core/CanDevice.cpp`：`configureChannel()`/`bitTimingPreview()` 的仲裁域与数据域
  都用新约定（clk 直接参与）；`recommendedClock()` 按硬件版本返回（V1.03+ → 80MHz）；
  `bitTimingPreview()` 去掉 `canClock` 字段。
- **删除标定表机制**（公式已精确，标定值对 4 个标准速率与公式完全等价 → 冗余）：
  移除 `src/core/BitCalibration.{h,cpp}`、`tests/test_bitcalibration.cpp`、QSettings 读写与
  迁移逻辑、界面「已标定」前缀（`DevicePanel.qml`）；`device_calibrate` 变为**只诊断不落盘**
  （打印实测可用参数，供界面「原始位定时（高级，覆盖公式）」填入）。
- `src/qml/DevicePanel.qml`：位定时文本改为
  `80 MHz · 125000 bps · brp=39 tseg1=12 tseg2=1 sjw=1 · SP 87.5%`；原始位定时默认值更新。
- 工具：`device_calibrate` 候选改为「总 Tq 数 × 精确整除」，择优 = ACK → 总 Tq=16 标准形状 → 帧数
  （零命中候选记为 0 分，避免加分项把"没通的组合"选成结果）；
  `device_baud_scan` 用 `paramsForTq` 枚举；`device_smoke`/`device_ts_probe` 去掉 0.8× 用法。
- 测试：`test_bittiming` 按新约定重写（4 档实测值断言 + ZLG demo 值 + `paramsForTq` 用例）。

**验证证据**：
- `ctest` **13/13 通过**。
- `QT_QPA_PLATFORM=offscreen ./build/CanDebugger` 无 QML 告警；遗留的 QSettings
  `[calibration]` 组已清理（备份 `/tmp/opencode/qsettings-with-calibration-backup.conf`）。
- 实机（对端 1M，80MHz，形状 tseg1=12/tseg2=1）：
  - `device_bitrate_scan 80 0 1 64 400 5 14` → **唯一命中 brp=4**（= 1M），与公式一致；
  - `device_calibrate 1000000 0 1000 8` → 选出 80MHz/`brp=4 tseg1=12 tseg2=1`（ACK 8/8，不落盘）；
  - `device_calibrate 500000`（对端仍在 1M）→ 正确报「未找到可用参数」（不再误报）。
- 500k：`device_multichannel_test`（走 `configureChannel` 公式路径）**通过**。
- 设备：`USBCANFD-200U HW=0x0300 FW=0x0252`，对端为另一块 ZLG CAN 卡（对端在 ch0 总线，ch1 无对端）。

**待办 / 下一步**：
- [ ] 无边框自绘标题栏手工复核（本次已落地，见「步骤 13」）：拖动移动、双击最大化/还原、
      四边四角缩放、最小化/关闭按钮、任务栏与 Alt-Tab 标题是否正常。
- [ ] App 里选 125k / 250k / 500k / 1M →「应用配置」→ 与对端互发复测（卡片只显示位定时，无「已标定」前缀了）。
- [ ] 数据域（CANFD 加速段）实测复核：80MHz 下公式给 2M→`brp1/tseg16/1`(SP 90%)、
      4M→`brp1/tseg6/1`(SP 80%)、5M→`brp1/tseg4/1`(SP 75%)。
- [ ] （可选）把 `device_calibrate` 的实测做成 App 内按钮。
- 已知噪声：对端流量**阵发**、`VCI_Transmit` 的 ACK 判据有抖动 → 诊断以 ACK 权重更高，
  被动有效帧为辅；偶发"发送失败"可将发送模式改「单次 / 自发自收」（不等 ACK）。
- 注意：**总线上的对端速率必须与本机一致**，否则本机可能进入错误态导致发送失败
  （例：对端 1M、本机配 500k 时 `device_multichannel_test` 的 ch0 会失败）。

---

## 步骤 0 — 工程骨架与 SDK 集成 ✅ 完成
- 产出：`CMakeLists.txt`、`resources.qrc`、`src/main.cpp`、`src/qml/{Main,Theme}.qml`、`include/zcan.h`、`lib/libusbcanfd.so`(+`.1.0.15`)
- 验证：
  - `cmake -B build && cmake --build build -j` → 构建成功
  - `QT_QPA_PLATFORM=offscreen ./build/CanDebugger` → 正常驻留，stderr 无 QML 错误
  - `readelf -d` 确认 RUNPATH = `$ORIGIN/lib`

## 步骤 1 — CanDevice 核心封装 ✅ 完成（已实机验证）
- 产出：`src/core/BitTiming.{h,cpp}`、`src/core/CanDevice.{h,cpp}`、`tests/test_bittiming.cpp`、`tests/device_smoke.cpp`
- 实现：打开/关闭、读设备信息、初始化/启动/复位/清缓冲、终端电阻、合并接收、
  收发（`VCI_Transmit(FD)` / `VCI_Receive(FD)` / `VCI_ReceiveData`）、位定时标准公式计算
- 并发：`CanDevice` 内 `QMutex` 串行化所有 SDK 调用（收发共用同一句柄）
- 验证：
  - `test_bittiming` **9 项全部通过**（500k→brp15/tseg1 6/tseg2 1、2M→brp2/12/2 等）
  - `device_smoke` 实机运行 **exit=0**：
    - 打开设备成功，SN=`7D42836140360B749690`，ID=`USBCANFD-200U`，HW=0x0300/FW=0x0252/API=0x0100，通道数=2
    - 通道0/1 均以标准公式配置并启动成功（500k/2M）
    - **自发自收**：发送 CAN 0x123 成功，接收计数=2（回显+自收），验证收发通路
  - 修复：位定时描述串 `%10` 占位符与 `.arg` 冲突导致“采样点 0000087.5”，改为分段格式化

## 步骤 2 — 接收通道 ✅ 完成（已实机验证）
- 产出：`src/core/FrameData.h`、`RingBuffer.{h,cpp}`、`RxWorker.{h,cpp}`、`RxManager.{h,cpp}`、`src/model/FrameListModel.{h,cpp}`、`tests/test_buffer_model.cpp`、`tests/test_rx_pipeline.cpp`
- 实现：独立 `QThread` 接收（非合并：逐通道 Receive/ReceiveFD；合并：ReceiveData），
  写入环形缓冲（覆盖最旧 + 丢弃计数），GUI 侧 33ms 定时批量 drain 入模型；
  模型保留上限可配（默认 10 万行）并正确裁剪；支持相对/绝对时间戳
- 验证：
  - `test_buffer_model` **7 项全部通过**（顺序/溢出/批量/角色/裁剪/清空/时间戳）
  - `ctest` 2/2 通过
  - `test_rx_pipeline` 实机运行 **exit=0**：自发自收 50 帧 →
    `接收=100 丢弃=0 模型行=100`，验证 设备→线程→环形缓冲→模型 全链路

## 步骤 3 — 设备面板 + 接收视图（QML） ✅ 完成（UI 运行验证）
- 产出：`src/qml/{DevicePanel,ReceiveView}.qml`，`Main.qml` 集成；`CanDevice`/`RxManager` 以
  上下文属性 `canDevice`/`rx` 暴露
- 实现：设备开关/信息、通道选择、仲裁+数据波特率、采样点滑块、终端电阻、合并接收、
  只听模式、应用并启动/停止；报文列表（表头+委托着色）、暂停/清空/自动滚动/时间戳模式/显示发送
- 验证：构建通过；真实 Wayland 显示下启动运行正常，stderr 无 QML 警告/错误

---

## 步骤 4 — 单帧发送 ✅ 完成（已实机验证）
- 产出：`src/core/FrameBuilder.{h,cpp}`、`src/qml/SendPanel.qml`、`tests/test_framebuilder.cpp`；
  `main.cpp` 暴露上下文属性 `frameBuilder`
- 实现：`FrameSpec` + 静态构建 `buildCan/buildFd/buildTtx`；数据编辑 Hex/ASCII/Dec
  （`parseData/formatData/convertData`，`dataByteCount` 校验）；数据生成 CRC-8（末字节，poly 0x07）、
  滚动计数（首字节）、递增填充（全字节，从 0 起）；标准/扩展、CAN/FD、数据/远程、BRS、
  DLC（0–8 / FD 0–64，FD 含 12/16/20/24/32/48）；发送模式 txm 四档 + echo；
  单帧发送与批量发送（`sendBatch`，C++/CLI 接口；QML 侧已改为「快捷周期发送」见步骤 16），
  均走 `canDevice.transmitCan/transmitFd`
- 数据语义：以 DLC 为准，数据不足补零、超出截断；远程帧不带数据但保留 DLC
- 验证：
  - `test_framebuilder` **12 项全部通过**（Hex/ASCII/Dec、错误输入、ID 掩码、CRC 已知值 0xF4、
    CAN/FD 字段映射、远程、补零/截断、递增/滚动/CRC 生成、specFromMap/describe）
  - `device_send_test` 实机自收：标准 CAN `0x123` 与扩展 CANFD `0x1ABCDEF0`（12 字节 BRS）
    均按输入字段正确收到

## 步骤 5 — 定时发送 ✅ 完成（已实机验证；**后续已改为软件定时，见步骤 14**）
- 产出：`src/core/PeriodicSender.{h,cpp}`、`src/qml/PeriodicPanel.qml`、`tests/test_periodicsender.cpp`；
  `CanDevice` 新增 `setPeriodicConfig/setPeriodicEnabled`（并备有 `getSendQueueSize/Space`、
  `enableSendQueue`、`clearSendQueue`）；`main.cpp` 暴露 `periodicSender`
- 实现：QML 表格增删改（≤8 条），每条 使能/周期(ms)/次数(0=循环)/ID/扩展/FD/DLC/数据；
  `apply` 构建 `ZCAN_TTX_CFG` 经 `CMD_CAN_TTX`(0x16) 下发，`start/stop` 经 `CMD_CAN_TTX_CTL`(0x17)
  整体启停，`setRowEnabled` 单条启停（重下发整表）
- 关键修正：**`ZCAN_TTX_CFG.size` 必须取字节数 `sizeof(ZCAN_TTX) * n`**。
  `zcan.h` 注释写“实际生效的数组的长度”有误导，实测按条目数会下发失败；
  以 SDK demo `test.c:auto_send` / `USBCANFD_DEMO.py` 为准
- 验证：
  - `test_periodicsender` **6 项全部通过**（周期换算、字段/索引/flags、FD 帧、>8 拒绝、
    空表、非法数据）
  - `device_send_test` 实机：2 条 100ms 定时发送运行 700ms 收到 28 帧，`0x500`/`0x501` 均收到；
    停止、单条禁用重下发均成功

## 步骤 6 — 滤波（硬件 + 软件） ✅ 完成（已实机验证）
- 产出：`src/core/CanFilter.{h,cpp}`、`src/model/FrameFilterProxyModel.{h,cpp}`、
  `ReceiveView.qml` 滤波区、`tests/test_canfilter.cpp`、`tests/device_filter_test.cpp`；
  `FrameListModel` 新增 `frameAt()`（+`IsEchoRole`）；`CanDevice` 新增 `setHardwareFilter`
- 实现：
  - 软件滤波（`FrameFilterConfig`）：按 ID（单值/范围，取帧自身 11/29 位掩码）、方向（全部/仅收/仅发）、
    类型（标准/扩展）、格式（CAN/FD/远程/错误）过滤；`CanFilter::matchesConfig()` 为纯函数便于单测
  - **模型级过滤**：新增 `FrameFilterProxyModel`（`QSortFilterProxyModel`），`rx.frameModel` 改为代理，
    隐藏行不再进入视图 —— **修复风险 6**，并删除委托里 `visible` 隐藏旧方案
  - 硬件验收滤波：`CanFilter::buildHardwareTable()` 生成 `ZCAN_FILTER_TABLE`
    （`sid..eid` 闭区间；`size` 取**字节数** `sizeof(ZCAN_FILTER)*n`，与 demo 一致），
    经 `CanDevice::setHardwareFilter`（`CMD_CAN_FILTER`）下发
  - QML：`rx.filter` 暴露；滤波区含 ID 范围、方向、格式勾选、硬件起止范围与「应用/清除」
- 关键实测结论（`device_filter_test`）：
  - 无滤波时自发自收每帧收到 **2 份**：总线接收（`tx=0`）+ 本机回环（`tx=1`）
  - 下发仅 `0x200` 的硬件滤波后：不匹配 ID 的**总线帧被滤除**（`tx=0` 计数=0），匹配 ID 总线帧保留；
    本机发送回环（`tx=1`）**不受验收滤波影响**（固件行为，属正常）
  - 软件滤波即时生效：仅保留 `0x300` 时其余 ID 行数为 0
- 验证：
  - `test_canfilter` **11 项全部通过**（范围/单值、标准 vs 扩展掩码、方向、类型、格式、
    硬件表字节数/截断交换/禁用/超 64 组、`rulesFromList`）
  - `device_filter_test` 实机 **exit=0**：基线自收、硬件仅 `0x200`、清除后恢复、软件仅 `0x300`、统计联动均通过

## 步骤 7 — 统计引擎 Statistics ✅ 完成（已实机联动验证）
- 产出：`src/core/Statistics.{h,cpp}`、`tests/test_statistics.cpp`；`CanDevice::readErrInfo`；
  `FrameBuilder::setStatistics`（成功发送累计）、`RxManager::setStatistics`（drain 按批注入）；
  `main.cpp` 暴露 `statistics` 并每 100ms 聚合；`ReceiveView` 状态栏预览标签（步骤 9 面板除外）
- 实现：
  - 接收管线：`RxManager::drain()` 将本批 `QVector<FrameData>` 交给 `Statistics::addFrames()`（**不新增逐帧信号**）
  - 发送侧：`FrameBuilder` 成功后 `countSent()` 累计；`totalFrames = rxCount + txCount`
  - 每 1s 聚合：收/发帧率、总线负载率、总帧数、错误帧数、按 ID Top-N（默认 10，可配）
  - 负载率：按帧位长估算 `frameBitSplit()`（标准帧开销 47bit、扩展 67bit、FD +20bit；
    FD+BRS 数据段按数据域波特率），仲裁/数据波特率由 DevicePanel 配置时写入，结果钳制 0–100%
  - 错误：管线错误帧计入 `errorFrames`；曾另经 `VCI_ReadErrInfo` 轮询 `busErrorCount`，
    后因该调用单次阻塞约 1s 拖死 GUI 而移除（见风险 18）
  - 暴露 `Q_PROPERTY` + NOTIFY（`updated`/`topNChanged`/`bitratesChanged`），QML 可绑定
- 验证：
  - `test_statistics` **9 项全部通过**（fps 公式、经典/FD 位长、rx/错误计数、发送计数与总帧、
    窗口速率与清零、负载率、Top-N 排序与更新、reset）
  - `device_filter_test` 统计联动：结束 `总帧=8 收=8 发=0`（自收回环帧 `tx=1` 只计入总线流量）
  - `ctest` 6/6 通过；QML offscreen 启动无错误

---

## 步骤 8 — 高性能绘图 RealtimePlot（QQuickItem） ✅ 完成（离线验证）
- 产出：`src/view/PlotRing.h`（纯逻辑环形缓冲，仅 QtCore/QVector）、
  `src/view/RealtimePlot.{h,cpp}`（QQuickItem 场景图自绘）、`tests/test_plotring.cpp`；
  `main.cpp` 注册 `qmlRegisterType<RealtimePlot>("CanDebugger",1,0,"RealtimePlot")`
- 实现：
  - `PlotRing`：多序列固定容量环形缓冲，满则覆盖最旧；支持增删序列、改容量（保留最新点）、
    极值查询；索引 0 为最旧点。纯头文件，便于脱离场景图单测
  - `RealtimePlot`：`QSGGeometryNode` 自绘，属性 `series`/`maxPoints`/`autoScale`/`fill`/
    `lineWidth`/`lineColors`/`minValue`/`maxValue`；`Q_INVOKABLE append(series,value)` / `clear()`
  - 折线用逐段四边形（`DrawTriangles`）实现可控线宽（不依赖 `GL_LINE_WIDTH`，各后端一致）；
    填充用三角带（`DrawTriangleStrip`）；单点时绘制小方块
  - 多序列：每个序列 1 个折线节点（+可选 1 个填充节点），固定容量、复用节点，
    仅在几何/材质变化时 `markDirty`；`series`/`fill` 变化才重建节点（`QSGNode::OwnedByParent`）
  - 线程：`QMutex` 保护 `PlotRing`；`append()` 写数据后 `update()`（自动合并，不逐点重绘）；
    `updatePaintNode()` 仅在锁内做快照拷贝，几何构建在锁外，不阻塞 GUI 追加
  - 绘制采用「最新点靠右、向左滚动」的映射，值域自动缩放（含 5% 边距）或固定
- 验证：
  - `test_plotring` **7 项全部通过**（基本读写、溢出保留最新、多序列、非法序列、
    增删序列、改容量保留最新、清空）
  - `ctest` **7/7 通过**
  - `QT_QPA_PLATFORM=offscreen` 启动 6s（统计每 100ms 推送、持续向图追加），
    stderr 无 QML 警告/错误
- 关键约束复核：未修改 `candbg_core` 依赖；`RealtimePlot.cpp` 仅加入 CanDebugger 可执行目标
  （链接 `Qt5::Quick`）；每帧 `allocate` 固定规模顶点，不进行大数组反复分配

## 步骤 9 — 统计面板 StatisticsPanel（QML） ✅ 完成（离线验证）
- 产出：`src/qml/StatisticsPanel.qml`；`Main.qml` 右侧 TabBar 增加第三页签「统计」；
  `resources.qrc` 登记
- 实现：
  - 数值卡片：总线负载率、收帧率、发帧率、错误帧、总帧数
  - 2 个 `RealtimePlot` 趋势：收/发帧率（2 序列，自动缩放）、总线负载率（0–100 固定量程、填充）
  - 报文 Top-N 列表（`statistics.topN`，字段 `idText`/`extended`/`count`）+ 复位按钮
    （`statistics.reset()` 同时清空曲线）
  - 数据接线：QML `Connections { target: statistics; onUpdated }` 在每次聚合后向图 `append`，
    **未在 C++ 新增逐帧信号**
  - `ReceiveView` 移除重复的统计预览标签（rx 计数标签保留）
- 验证：构建通过；`ctest` 7/7；`QT_QPA_PLATFORM=offscreen` 启动无 QML 警告；
  `import CanDebugger 1.0` 解析正常（自定义类型已注册）

---

## 步骤 10 — 记录器 Recorder（ASC + CSV） ✅ 完成（离线 + 实机验证）
- 产出：`src/core/Recorder.{h,cpp}`、`tests/test_recorder.cpp`；
  `RxManager::setRecorder`、`FrameBuilder::setRecorder`、`main.cpp` 暴露上下文属性 `recorder`
- 实现：
  - 生产者（GUI 线程：`RxManager::drain()` 批 + `FrameBuilder` 发送路径）**非阻塞入队**；
    独立写盘线程（`std::thread` + `QMutex`/`QWaitCondition`）消费落盘，不阻塞 UI
  - 双格式：ASC（CANoe 风格：`时间 通道 id[x] 方向 类型 dlc data`，含 `Begin/End Triggerblock`
    头尾）与 CSV（表头固定 `timestamp,channel,dir,id,type,dlc,data`）；`formatFlags` 可单选/同时写
  - 文件名 `record_yyyyMMdd_hhmmss.asc/.csv`；`start(dir,asc,csv)` 自动命名、
    `startWithPaths(asc,csv)` 显式路径；`stop()` 幂等，join 写盘线程后收尾
  - 时间戳用**主机时间 `wallUs`**（相对记录起点；风险 5：不依赖 `devTs`）
  - tx 口径（风险 17/19）：接收批中的 `tx` 帧（回显/回环）**跳过**，
    本机发送由 `FrameBuilder` 成功发送路径记录（`tx=true`），避免重复写入；
    记录注入在显示层软件滤波**之前**（风险 19）
  - 属性 `recording/formatFlags/ascPath/csvPath/filePath/queueCapacity/writtenCount/
    droppedCount/errorString` + NOTIFY；`writtenCount/droppedCount` 由 GUI 轮询定时器
    （200ms）发信号，避免跨线程 emit
- 验证：
  - `test_recorder` **10 项全部通过**：ASC/CSV 格式化↔解析往返（标准/扩展/FD/BRS/远程/错误）、
    表头跳过、文件名、写盘后重解析 **行数/字段/顺序一致**、接收 tx 跳过、
    队列溢出 `written+dropped` 守恒且不阻塞（丢弃递增）、start/stop 幂等、失败返回 errorString
  - `ctest` 9/9 通过

## 步骤 11 — 回放器 Replayer ✅ 完成（离线 + 实机验证）
- 产出：`src/core/Replayer.{h,cpp}`、`tests/test_replayer.cpp`、`tests/device_replay_test.cpp`（实机）；
  `main.cpp` 暴露上下文属性 `replayer`
- 实现：
  - 自动识别 ASC/CSV（扩展名优先，内容嗅探回退）；解析为 `QVector<FrameData>`，
    复用 `Recorder::parseAscLine/parseCsvLine` 保证与记录格式**单一来源**
  - 归一化时间轴（首帧为 0）；GUI 侧 `QTimer`(5ms) 按 `dueFrameCount()` 计算到期帧并
    **非阻塞入队**，独立发送线程调用 `canDevice.transmitCan/transmitFd`
  - **发送必须在后台线程**：实测 `VCI_Transmit` 在接收线程并发时可能阻塞数秒
    （`device_send_test` 总耗时 ~40s），GUI 线程直接发送会违反“UI 不阻塞”不变式
  - 倍速 `speed`、暂停/继续/停止、`positionUs/durationUs/progress`；暂停同时挂起发送线程；
    错误帧跳过；错误经线程安全字段由 GUI 取出后 emit
  - 纯逻辑：`detectFormat/parseText/parseFile/normalizeTimeline/totalDurationUs/
    progressPercent/scaledDelayUs/dueFrameCount` 均可免设备单测
- 验证：
  - `test_replayer` **9 项全部通过**（扩展名/内容识别、CSV/ASC 解析字段、归一化与时长、
    进度与倍速换算、到期帧数调度、CSV 加载、Recorder→ASC→Replayer 闭环）
  - `ctest` 9/9 通过
  - `device_replay_test` 实机 **通过**：记录 2 帧（CAN 0x123、扩展 CANFD 0x1ABCDEF0 BRS）
    → 回放 ASC 与 CSV 经自发自收均按字段收到

## 步骤 12 — 记录/回放面板 RecordPanel（QML） ✅ 完成（离线验证）
- 产出：`src/qml/RecordPanel.qml`；`Main.qml` 右侧 TabBar 第四页签「记录」；`resources.qrc` 登记
- 实现：
  - 记录：目录输入、ASC/CSV 勾选、开始/停止、状态灯、已写/丢弃计数、文件路径与错误提示
  - 回放：文件路径可编辑 `TextField`（`QtQuick.Dialogs` 运行时不具备，按风险 2 退化，不 import）、
    加载、通道/倍速(0.5/1/2/5/10x)、播放/暂停/继续/停止、进度条与位置/时长
  - 接线 `recorder`/`replayer` 上下文属性；中文文案、Theme 传递
- 验证：构建通过；`ctest` 9/9；`QT_QPA_PLATFORM=offscreen` 启动驻留 5s，stderr 无 QML 警告/错误
  （修复了记录状态灯 anchors-in-layout 警告）

## 步骤 13 — 无边框自绘标题栏 ✅ 完成（离线验证 + X11 交互实测）
- 产出：`src/qml/Main.qml`（`Qt.FramelessWindowHint`，标题栏与原工具条合并为一行）、
  `src/qml/components/WindowButton.qml`（新增）、`Icon.qml` 新增 4 个窗口按钮图标、
  `Theme.qml` 新增标题栏令牌（高度/按钮宽/hover/按下/关闭红/缩放热区/拖动阈值）、
  `resources.qrc` 登记新组件；`main.cpp` 注入 `platformName` 上下文属性
- 实现：
  - 标题栏：品牌 + 版本 + 设备状态徽章 + 记录中/帮助/设置 + 右侧最小化/最大化-还原/关闭；
    非活动窗口标题栏变暗（`window.active`）
  - 交互：空白区拖动移动窗口、双击最大化/还原、四边+四角 5px 热区缩放（带对角光标）、
    最大化/全屏时热区整体隐藏；拖动有 4px 阈值，避免吃掉单击/双击
  - **拖动/缩放按后端分流**（关键坑）：Wayland 交给合成器
    （`startSystemMove`/`startSystemResize`，吸附/平铺由 WM 完成）；X11 下 Qt 5.15 只是发一条
    `_NET_WM_MOVERESIZE` 就无条件返回 `true`，实测 KWin 忽略运动事件触发的移动请求
    （调用成功但窗口纹丝不动）→ X11 改为 QML 自算 `window.x/y/width/height`，不依赖 WM
  - 顺带打磨：按钮类补 `Qt.PointingHandCursor`、`AppButton` 焦点环、报文表空态占位文案
- 验证：
  - `ctest` **13/13 通过**（`tst_panels` 编译清单加了 `WindowButton`）
  - `QT_QPA_PLATFORM=offscreen ./build/CanDebugger` 无 QML 报错
  - 真实会话（KDE + Wayland，用 `QT_QPA_PLATFORM=xcb` 走 X11 分支）通过 `xdotool` 注入指针事件实测：
    标题栏拖动位移与指针位移一致、四边/四角缩放尺寸按拖距变化、双击最大化/还原往返正常、
    最小化/最大化/关闭按钮生效（关闭后进程退出）
  - 测试脚本（未入库）：`/tmp/opencode/verify_x11.sh`（真实显示）、
    `/tmp/opencode/run_isolated.sh` + `/tmp/opencode/verify_window.sh`（Xvfb + KWin 隔离环境）

---

## 步骤 14 — 周期发送改为软件定时（双通道标签页） ✅ 完成（离线 + TSan/ASan 验证）
- 背景：原实现走固件定时表（`CMD_CAN_TTX`），受 `ZCAN_TTX_CFG.table[8]` 硬限制，最多 8 条
- 产出：重写 `src/core/PeriodicSender.{h,cpp}`、重写 `src/qml/PeriodicPanel.qml`、
  重写 `tests/test_periodicsender.cpp`；`main.cpp` 注入 `statistics`/`recorder`；
  `Recorder::m_recording` 改为原子量
- 实现：
  - **PC 侧精确定时**：不再使用 `CMD_CAN_TTX`/`CMD_CAN_TTX_CTL`，改为 `steady_clock` 绝对时间调度
    + `condition_variable` 定时等待；条数不再受限
  - **线程模型**（2 条常驻线程，与通道数无关）：调度线程只算到期条目并入有界队列（从不调 SDK）；
    发送线程是唯一做阻塞式 `VCI_Transmit` 的地方。因此 GUI 与调度都不受发送阻塞影响
  - **防漂移 + 落后丢弃**：正常时 `nextDue += interval`（不累积漂移）；落后超过一个周期时
    丢弃落后时隙并 `nextDue = now + interval`（不突发补发）
  - **启动立即发一次**：`start()` 以启动时刻为基准写入 `nextDue`，之后按周期推进
  - **双通道独立**：每通道各自一张表、各自启停、各自错误；`channelStates` 单通知暴露给 QML
  - **UI**：`PeriodicPanel` 的通道下拉框改为顶部内联标签页（复用 `ToolbarButton`），
    每标签显示运行态圆点与条数；通道各自独立的 `ListModel` 与调度
  - **统计/记录接入**：发送成功计入 `Statistics::countSent`（因该接口非线程安全，
    发送线程只做原子累计、批量回投 GUI 线程）并按 `FrameBuilder` 口径写入 `Recorder`
  - 硬件接口 `CanDevice::setPeriodicConfig/setPeriodicEnabled` **保留但不再调用**（便于回退）
- 验证：
  - `ctest` **13/13 通过**；`test_periodicsender` **25 项全通过**（含纯逻辑 17 项 +
    调度/发送/线程路径 7 项，后者经测试缝 `setTransmitBackend` 注入无硬件替身）
  - **ASan 0 错误/泄漏**（覆盖真实调度与发送线程）
  - ⚠️ TSan：本会话复测**并非稳定 0 告警**（20 次连跑共 2 条，约 40% 的运行会出现 1–2 条）。
    已用最小复现程序定位：**只做 `QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)`
    跨线程回投就会报**（15/15 次复现），raced 内存是 Qt 的 functor 载荷
    （`qobjectdefs_impl.h`），与 `PeriodicSender` 的业务逻辑无关 ——
    同「风险 5/Qt 5.15 QMutex 为 futex 实现、TSan 无法建立 happens-before」一类，
    根因是 `libQt5Core` 未插桩。处理方式见「步骤 15」待办。
  - `QT_QPA_PLATFORM=offscreen ./build/CanDebugger` 无 QML 报错
- 未验证（需实机）：真实总线上的周期精度、`VCI_Transmit` 阻塞下的队列行为、
  以及 >8 条的实机并发发送

---

## 步骤 15 — 周期发送批量提交（同 tick 帧合并为一次 USB 调用） ✅ 完成（实机前后对比）
- 背景（实机反馈）：一张 10 条、周期均 100ms 的周期表，表内相邻报文在总线上间隔约 **1ms**，
  即整表首尾被拉开约 10ms（占 100ms 周期的 10%）。
- 根因：发送线程对**每一帧**各调一次阻塞式 `VCI_Transmit`（`count=1`），每帧一次 USB 往返
  （实测 ~0.7–1ms/帧），与周期值无关；总线本身远没这么慢（500k 经典 8 字节帧约 0.24ms）。
- 实机探针（`test_ui_blocking --diag-batch`，自发自收，10 帧）：
  | 方式 | 10 帧总耗时 | 折合每帧 |
  |---|---|---|
  | 逐帧 10 次 `VCI_Transmit` | 7.64 ms | **0.76 ms** |
  | 一次 `VCI_Transmit(count=10)` | 3.33 ms | **0.33 ms** |
  | 一次 `VCI_TransmitFD(count=10)` | 2.30 ms | **0.23 ms** |
  无 ACK 总线上：逐帧 204 ms（10×20ms 超时）vs 合批 20.5 ms —— **超时按调用计一次，不按帧数累加**；
  `ret` 为**实际成功提交的帧数**（成功 10/10、无 ACK 0/10、256 帧一次调用 256/256）。
- 改动：
  - `CanDevice` 新增 `transmitBatchCan/transmitBatchFd(port, msgs, count)`；`transmitCan/transmitFd`
    改为其 `count=1` 封装，去掉重复实现
  - `PeriodicSender`：发送线程改为**成批取队列**（同一通道连续段、上限 `kBatchMax=64`），
    一批交给后端一次提交；`splitSpecIndicesByType()` 把一批按类型拆成 CAN / CAN-FD 两组
    （组内保序，**CAN 组在前**），故一批最多 2 次 USB 调用；跨类型相对顺序可能变化
    —— 明确取舍：优先减少调用次数（用户选定）
  - 测试缝 `setTransmitBackend` 的发送回调改为**批量语义**且返回**逐帧结果**
    （`QVector<bool>`，与 specs 等长）；因为拆批后提交顺序 ≠ 表内顺序，只有逐帧结果才能准确记账。
    `markGroupSubmitted()` 负责「组内前 n 帧成功」→ 原下标的映射（纯函数，已单测）
  - 记账：仅对成功提交的帧计入 `Statistics::countSent`、写入 `Recorder`；部分失败时通知
    文案给出「已发出 x/y」；整批失败沿用「总线无应答…」提示
- 验证：
  - `ctest` **13/13**；`test_periodicsender` **31 项全通过**（新增：合批 `maxBatch/calls`、
    部分失败、整批硬失败、100 条按 64 切分、`splitSpecIndicesByType`、`markGroupSubmitted`）
  - **ASan 0 错误/泄漏**；TSan 与步骤 14 同量级（9/20 次运行），新代码未引入新的告警类别
  - 新增实机探针 `./build/device_periodic_test [条数] [周期ms] [运行ms]`：自发自收回灌，
    按设备时间戳（100us/tick）算同 tick 内相邻帧间隔
    | 10 条 × 100ms | 同 tick 相邻帧间隔（中位/范围） | tick 周期 |
    |---|---|---|
    | 改造前（逐帧，等价 `kBatchMax=1`） | **0.700 ms**（0.6–0.8） | 100.00 ms |
    | 改造后（合批） | **0.300 ms**（0.2–0.4） | 100.00 ms |
    已到 500k 经典 8 字节帧的总线极限；每 tick 稳定抽满整表（12 个满表 tick / 13）
  - `device_send_test` 通过；`device_stress_test`（8 条 @5ms）**3402 fps、丢弃 0**
    （改造前逐帧路径的 USB 往返上限约 1000–1300 fps）
- 待办：TSan 的 Qt functor 假阳性要么加 suppressions 文件，要么把跨线程回投改成
  GUI 线程定时器轮询（去掉 `invokeMethod(functor)` 与逐条通知的堆分配）

---

## 步骤 16 — 单帧面板「快捷周期发送」（删除多帧列表） ✅ 完成（离线验证）
- 背景（界面反馈）：单帧发送右侧的「加入列表 / 高级（多帧列表）」实际是坏的 ——
  `addToList()` 调用 `frameBuilder.describe(spec)`，而 `describe` 是普通静态方法（非
  `Q_INVOKABLE`），不在元对象里，QML 调用必然 `TypeError`；由于异常发生在 `listModel.append()`
  的实参求值阶段，列表**永远加不进任何条目**，于是「发送全部/发送选中」也全部失效。
  展开高度也只有约 20px（`SendWorkspace` 固定 256 高），即使修好也几乎看不见。
- 决策（用户选定）：**删掉多帧列表**，改为单帧面板自带的「快捷周期发送」；与周期页签
  的配置表**完全互不干扰**；后端复用 `PeriodicSender` 的排期/批量/卡顿保护，不另写定时器。
  这样也让「递增填充/滚动计数/CRC-8」真正有用（同一帧反复发送时滚动计数逐帧 +1）。
- 改动：
  - `SendPanel.qml`：删除 `advanced`、多帧列表卡片、`listModel/addToList/batchList/sendAll/
    sendSelected`；「快捷周期」控件**并排进「数据 + 发送」那一行**（周期 ms + 次数 + 周期发送/
    停止按钮，长提示改为按钮 ToolTip），默认 **周期 100ms / 次数 1 次**（0 = 循环，留空按 1 次）。
    状态行改为由属性驱动的 `statusText()/statusColor()`（保持对 `canDevice.open` 的绑定，
    不再被命令式赋值打断）
  - `SendWorkspace.qml` / `Main.qml`：底部发送区高度按页签区分 —— 单帧页签 215（收紧 41px
    给接收列表），周期/记录回放 256（表格高度不变）
  - `PeriodicSender`：新增**快捷槽** `m_quick[2]`（每物理通道一个，恒 1 帧），与表通道
    `m_channels[2]` 共用调度/发送线程；新增 `quickStates` 属性与
    `startQuick/stopQuick/quickActive` 接口、`quickStatesChanged/quickOperationFinished` 信号。
    调度器把原循环体抽成 `processSlotSet()` / `stopSlotSet()` 作用于两组槽位；
    `SendItem/Notice` 增加 `quick` 标记，`purgeSendQueue(port, quick)` 按槽位清队列，
    发送线程只合并「同通道 + 同槽位」的连续段，保证失败/积压通知各自归位
  - `AppTextField`：补禁用态 `opacity`（运行中锁定周期/次数输入）
- 验证：
  - `ctest` **13/13**；`test_periodicsender` **38 项全通过**（新增 7 项：快捷槽次数/周期节奏、
    滚动计数逐帧 0/1/2 与 `increment/crc/txm` 透传、与表通道互不干扰、设备断开自动停、
    非法帧描述拒绝启动、`quickStates` 内容、`stopAll` 一并停）
  - `tst_qml` **23 项全通过**（新增：快捷周期默认值、次数留空/非法回退 1 次、离线启动被拒并
    给出原因且不串到周期页签、"数据+发送"行不超宽 + 状态行在可视区内 + 按页签高度 215/256）
  - `QT_QPA_PLATFORM=offscreen ./build/CanDebugger` 无 QML 警告
- 已知取舍：运行中不给 GUI 逐帧推状态（100ms 周期的信号风暴无意义），帧数看接收列表/统计；
  周期/次数改动需「停止」后重新启动；同通道同时跑周期表与快捷槽时，发送顺序由 `CanDevice`
  的 IO 锁决定，不做承诺
- 顺带发现（**未处理，待定**）：底部单帧面板的**最小宽度为 829px**（由第 1 行"帧参数"决定，
  与本次改动无关），而 1080 最小窗口下发送区只有 794−20=774px，故最小窗口时第 1/3 行右端
  （发送模式 / CRC-8）会被窗口裁掉约 55px。本次已让"数据+发送"行自身只需约 620px（数据框
  `Layout.minimumWidth: 120`）并加了回归断言；要彻底修需给第 1 行的下拉框加
  `Layout.minimumWidth` 并去掉第 3 行冗余的"数据格式"标签（约 3 行改动），等确认后再做

## 步骤 17 — 「ID 递增」开关 ✅ 完成（离线验证）
- 背景（界面提问）：三个"数据生成"开关都只作用于**数据字节**（递增填充 = 数据 `00 01 02…`、
  滚动计数 = 数据首字节、CRC-8 = 数据末字节），**没有任何按帧推进 ID 的能力**。
- 改动：
  - `FrameSpec` 新增 `incrementId`；`specFromMap/specToMap` 读写 `incrementId`
  - 新增纯函数 `FrameBuilder::idForCounter(baseId, extended, incrementId, counter)`：
    不勾选时等价 `clampId(baseId)`；勾选时 `(基址 + 计数器) & 位宽掩码`（标准 0x7FF /
    扩展 0x1FFFFFFF 回绕）
  - 三条发送路径都在"赋计数器"之后、**构建帧之前**把结果写回 `spec.id`，因此下发帧、
    回显/记录帧（`FrameData.id`）、`describe()` 口径完全一致：
    `FrameBuilder::sendFrame`（同步）、`FrameBuilder::enqueueOne`（异步）、
    `PeriodicSender::processSlotSet`（周期表 + 快捷槽共走这里）
  - 计数器推进条件由 `rolling` 放宽为 `rolling || incrementId`（两者共用一个计数器：
    单帧路径 `m_counter`、周期/快捷路径各槽位自己的 `PeriodicRow.counter`）
  - `SendPanel.qml`：新增「ID 递增」勾选框并进入 `specMap()`；为不让选项行变宽，
    去掉冗余的"数据格式"标签（Hex/ASCII/十进制 已自明）
- 验证：
  - `test_framebuilder` **30 项全通过**（新增：`idIncrement` 标准/扩展回绕与基址掩码、
    `idIncrementBuildCan`、`specToMap/specFromMap` 往返带 `incrementId`）
  - `test_periodicsender` **40 项全通过**（新增：快捷槽 ID 递增 `0x7FE→0x7FF→0x000` 回绕、
    ID 递增与滚动计数共用计数器同步推进）
  - `tst_qml` **24 项全通过**（新增：`specMap()` 必须带 `incrementId` 且不顺带打开其他开关）
  - 选项行隐式宽度 777 → 806（< 面板既有最小宽度 829，未撑大最小值）
- 语义提醒：ID 递增与滚动计数共用计数器，同时勾选时**每帧既换 ID 又换数据首字节**；
  若只想换 ID，请单独勾「ID 递增」

---

## 实机验证汇总
- 已安装 udev 规则：`/etc/udev/rules.d/50-usbcanfd.rules`，设备节点变为 `crw-rw-rw- root:users`，普通用户可访问
- `device_smoke`、`test_rx_pipeline`、`device_send_test`、`device_filter_test`、`device_replay_test`、
  `CanDebugger` 均通过
- **发送阻塞实测（重要）**：设备 ch1↔ch2 已外部短接后发送可成功；但接收线程并发时
  `VCI_Transmit` 单次可能阻塞 1–4s（`device_send_test` 端到端 ~40s，`device_smoke` 无接收线程仅 ~2.5s）。
  因此回放发送放在**独立线程**，GUI 不阻塞（见风险 29）
- 标准公式（clk=60MHz）配置的 500k/2M 在本机自收自测可用（未接外部节点）
- 发送链路自收验证：单帧 CAN/FD 字段一致；定时发送按 100ms 周期发出并可启停
- 滤波实机验证：硬件验收滤波有效滤除不匹配 ID 的总线帧（本机回环不受影响）；软件模型滤波即时生效
- 统计实机联动：帧计数/方向/总线流量随活动变化合理

## 待办 / 下一步
- [x] 安装 udev 规则并实机验证步骤 1–3
- [x] 里程碑 2：步骤 4–5（单帧/定时发送）并实机自收验证
- [x] 里程碑 3（部分）：步骤 6 滤波、步骤 7 统计引擎并实机自收验证
- [x] 里程碑 4：步骤 8 RealtimePlot / 步骤 9 StatisticsPanel（离线验证）
- [x] 里程碑 5：步骤 10–12（记录器 / 回放器 / 记录回放面板），实机 `device_replay_test` 通过
- [ ] 实机高帧率下核验绘图流畅度与 CPU 占用（1000 点/秒验收项，当前为离线/结构验证）
- [ ] 接外部 CAN 节点，核验标准公式 500k/2M 的互操作性（对端若用 demo 参数可能不匹配）
- [x] 记录周期性发送帧（步骤 14：软件定时发送已按 `FrameBuilder` 口径写入 `Recorder`）

## 已知说明
- 收发共用设备句柄，`CanDevice` 用互斥锁串行化；接收线程持锁最长 `timeout`(5ms)
- 高帧率下若 `接收` 增长快于 UI 刷新，环形缓冲会丢弃最旧帧并计数（`丢弃`）
- 发送/回显帧通过 `hdr.inf.tx` 区分；列表可隐藏发送帧

## 已知风险 / 技术债（新会话必读）
1. **波特率互操作性（已定论并修复，见风险 30）**：SDK 下发位定时的约定是
   `baud = clk / ((brp+1) * (tseg1+tseg2+3))`（字段=寄存器值=有效值−1，`clk` 即真实时钟），
   与普通 CAN 教科书的 `clk/(brp*(1+tseg1+tseg2))` 差在"三个字段各自 −1"。早期用 0.8×
   名义时钟的近似模型只能凑出 250k/500k，125k/1M 会偏 6.7%/16.7%（现象：本机自收自测正常、
   接外部节点失效）。现公式已精确、四速率实测验证；标定表机制已**删除**（与公式等价、冗余），
   非标准速率用界面「原始位定时（高级，覆盖公式）」手工填，或用 `device_calibrate <bps>`
   现场实测出参数（只诊断不落盘）。
2. **SDK 线程安全未知 / 锁分层（已重构）**：`libusbcanfd.so` 为预编译二进制，句柄访问
   仍由 `CanDevice` 串行化，但已按数据面/控制面拆锁：
   - `m_rxMutex`：仅 `receive*`（可在阻塞式 `VCI_Receive` 期间长持锁，无人争用）；
   - `m_ioMutex`：`transmit*` + `get/setReference` + 发送队列（短调用，GUI 走这里）；
   - 生命周期 `open/close/init/start/reset/clearBuffer` 同时持 rx+io，与接收串行。
   修复了「RX 持锁阻塞等待 + 解锁即重锁 + 非公平锁」导致的护航/饥饿：实测 RX 运行下
   `ioLockWait` 由最坏 ~1.9s（空闲）/中位 344ms（负载）降到 0；`setPeriodicConfig`
   由最坏 21s 降到 <1ms。并发 `Receive ∥ Transmit/SetReference` 已由
   `device_stress_test`（~3400fps、丢弃 0）、`device_send_test`、`device_multichannel_test`、
   `device_filter_test` 验证。若后续发现 SDK 不支持并发，退回方案：单锁 + `VCI_GetReceiveNum`
   门控 + `VCI_Receive(timeout=0)` 非阻塞轮询。
3. **合并接收**：`setRecvMerge(0,true)` 后 worker 走 `VCI_ReceiveData(port=0)`；UI 在勾选
   合并时禁用通道选择。合并模式的错误帧/取消合并路径未充分测试。
4. **未做压力/泄漏测试**：计划步骤 2 验收含“1000fps 稳定、无内存泄漏”，因无外部节点未验证；
   仅有 50 帧自收自测。
5. **`hdr.ts` 设备时间戳已标定并启用**：单位 = **100us (0.1ms)**/tick；32 位自由运行计数器，
   双通道共用同一时钟，约 4.97 天回绕。由 `tests/device_ts_probe.cpp` 实测（100ms 间隔
   Δts≈1057，ratio=0.0100；跨通道两帧差值仅 8 tick）。`FrameListModel` 相对时间已优先使用
   `devTs`（`FrameData::kDevTsTickUs`，含 U32 回绕安全差值与 `devTs==0` 主机时间回退）；
   绝对时间仍为主机挂钟。ASC/CSV 记录时间仍用主机 `wallUs`（相对记录起点），未改为设备时间。
6. **显示过滤（showTx）在 QML 委托用 `visible` 实现**，隐藏行仍占列表项；步骤 6 应改为模型级过滤。
7. **模型裁剪**：单批超过上限时丢弃本批最旧；`QList::erase` 为 O(n)，10 万行量级可接受。
8. **`RxManager::stop()` 在 GUI 线程最多阻塞 5s**（等待接收线程退出）；正常 <10ms。
9. **QML 未设置 QQuickStyle**（因未链接 `Qt5QuickControls2`），使用默认样式，视觉与 Fusion 有差异。
10. **`device_smoke` / `test_rx_pipeline` 依赖真实设备**，故意不注册进 ctest；`ctest` 只含纯单测。
11. **发送侧队列/超时未接入 UI**：批量发送已实现（`FrameBuilder::sendBatch`）；定时发送自步骤 14 起
    由 `PeriodicSender` 自己的软件调度线程与有界队列发送，不使用设备侧发送队列。
    发送队列/超时 API（`ZCAN_CMD_*_SEND_QUEUE_*`）已在
    `CanDevice` 提供（`getSendQueueSize/Space`、`enableSendQueue`、`clearSendQueue`），UI 暂未使用。
12. 排查提示：若运行报 `libusbcanfd.so.1.0.15: cannot open shared object`，检查 `lib/` 软链与 RPATH。
13. **定时发送配置语义**：`ZCAN_TTX_CFG.size` 取字节数 `sizeof(ZCAN_TTX) * n`（**非条目数**），
    已按 SDK demo 修正并实机验证；`interval` 单位 100us，UI 以 ms 输入换算（最小 1 unit=100us）。
14. **数据生成为本地约定**：CRC-8（poly 0x07、初值 0、写末字节）、滚动计数（写首字节）、
    递增填充（数据从 0 起）、ID 递增（`idForCounter()`：ID = 基址 + 计数器，标准帧按 0x7FF、
    扩展帧按 0x1FFFFFFF 回绕；与滚动计数**共用同一计数器**，故二者任一勾选都会逐帧 +1，
    单帧路径的 `m_counter` 自增条件已同步放宽为 `rolling || incrementId`。
    非通用协议，跨节点使用需与对端约定一致。
15. **硬件验收滤波语义（步骤 6 实测）**：`ZCAN_FILTER_TABLE.size` 取**字节数**
    `sizeof(ZCAN_FILTER)*n`（取条目数会导致 `VCI_SetReference` 失败）；`sid..eid` 为闭区间；
    验收滤波仅作用于**总线接收**帧，本机发送回环（`inf.tx=1`）不受影响。UI 在通道运行中
    下发即生效（实测），无需重启通道。
16. **软件滤波为显示层**：仅影响列表/统计注入前的显示，不减少 SDK 上报；只有硬件滤波能减少上报量。
    `FrameFilterProxyModel` 不保留被过滤行的视图项（修复风险 6）。
17. **统计发送侧口径**：`txCount` 由 `FrameBuilder` 单帧/批量发送与**软件定时发送**
    （步骤 14，发送成功即计入）累计；回放发送未计入。硬件定时发送（`CMD_CAN_TTX`）路径
    已不再调用。自收回环帧（`tx=1`）只计入总线负载与 Top-N，不计入 rx/tx。
    若回显关闭，负载率会低估本机发送量。
18. **负载率/错误计数为近似**：位长为估算（含填充位余量假设，非精确）；错误计数只用管线
    `errorFrames`（合并接收 `ZCAN_DT_ZCAN_ERROR_DATA` / 非合并 `hdr.inf.err` 置位）。
    **已移除** `busErrorCount` 及其轮询 `VCI_ReadErrInfo`：实测该调用单次阻塞约 1006ms，
    原被 `Statistics::pollErrors()` 放在 GUI 线程每 100ms 调用，导致启动接收后即使总线无帧
    也会界面卡死/无响应，并长时间持有 `CanDevice` 锁使接收停顿。
19. **接口变更（兼容性）**：`rx.frameModel` 现为 `FrameFilterProxyModel`（不再是 `FrameListModel`）；
    设备测试改用 `QAbstractItemModel::rowCount()`。`FrameListModel` 新增 `frameAt()` 与 `IsEchoRole`。
20. **RealtimePlot 线程/节点生命周期**：`PlotRing` 由 `QMutex` 保护；`updatePaintNode()` 仅在锁内
    快照拷贝，几何构建在锁外，锁持有时间短。QSG 子节点用 `OwnedByParent`，随根节点销毁释放；
    若后续引入多窗口/自定义渲染线程，需重新复核节点跨线程访问与释放路径。
21. **绘图性能验收未做实机压力**：步骤 8 验收含「1000 点/秒不掉帧、CPU 可控」，当前只做了离线运行
    与结构保证（O(1) 追加、`update()` 合并重绘、每帧固定规模 `allocate`、无逐点信号），
    **未在真实总线高帧率下实测 CPU/掉帧**；`fill` 模式每序列顶点数为 2N，序列多时需关注。
22. **自定义 QML 类型导入方式**：`StatisticsPanel` 依赖 `import CanDebugger 1.0`，由 `main.cpp`
    `qmlRegisterType` 运行时注册（非 qmldir 模块）。若改用 `qmlcachegen`/静态 QML 模块或
    `qt_add_qml_module`，需另行登记该类型；`lineColors` 以 QML 颜色数组传入，C++ 侧同时兼容
    `QColor` 与字符串色值。
23. **记录 tx 口径（步骤 10，步骤 14 起已扩展）**：为避免与回显/回环重复，**接收管线中的 `tx` 帧
    一律不写盘**，本机发送由发送路径记录：`FrameBuilder::sendFrame`（单帧/批量）与
    `PeriodicSender::sendOne`（软件定时，步骤 14）成功发送时均按同一口径写入 `Recorder`。
    回放发送不经过两者，仍不会被记录（与风险 17 同源）。
    回放时文件中的 `Tx`/`Rx` 帧都会被重新发送（回放=复现总线流量）。
24. **记录文件格式为本地约定**：ASC 为 CANoe **风格**（`时间 通道 id[x] 方向 kind dlc data`，
    `kind ∈ d/r/fd/fdb/fdr/e`），非严格 CANoe 全量格式；CSV 表头固定
    `timestamp,channel,dir,id,type,dlc,data`，扩展帧以 id 后缀 `x` 表示，`type ∈
    {CAN,CANFD,CANFD_BRS,CAN_RTR,CANFD_RTR,ERROR}`。`Recorder::format*/parse*` 与 `Replayer` 共用，
    保证往返一致；跨工具使用需确认列义与时间单位（本工具为相对秒，6 位小数）。
25. **写盘为后台线程 + 有界队列（默认 20 万）**：队列满丢弃新帧并累加 `droppedCount`，
    不阻塞生产端；`writtenCount/droppedCount` 由 GUI 轮询定时器（200ms）发 NOTIFY。
    写盘按批 flush，极端高帧率/慢磁盘下仍可能丢弃（可用 `queueCapacity` 调整）。
26. **回放调度与发送分离**：GUI 线程 `QTimer`(5ms) 仅做时间轴调度（`dueFrameCount`）并**非阻塞入队**；
    独立发送线程消费队列调用 SDK。倍速与原始间隔为近似（QTimer 粒度 + 设备发送耗时）；
    GUI 阻塞不再影响发送线程。暂停会同时挂起发送线程。错误帧被跳过。
27. **`VCI_Transmit` 发送阻塞（已定位并缓解）**：实测根因是驱动 `VCI_Transmit` 内部
    `wait(tx)` 等待总线 ACK，**无 ACK 时等到默认发送超时 ~1500ms**（自发自收/单次模式不等 ACK，
    ~0.65ms；与接收线程是否并发无关）。SDK 提供 `CMD_CAN_TX_TIMEOUT(0x44)`（单位 ms）控制该等待，
    超时后帧仍会提交（驱动日志 `wait timeout and is submit~`）。
    现由 `CanDevice::startChannel()` 启动后自动下发 `txTimeoutMs`（默认 20ms），
    `FrameBuilder` 的 GUI 同步发送最坏耗时从 ~1.5s 降到 ~20ms（首次约 125ms 为 TX 通路预热）。
    回归：`tests/device_tx_timeout_test.cpp`。
    **另已提供后台异步发送**：`FrameBuilder::sendFrameAsync/sendBatchAsync` + `sendCompleted`，
    QML「发送」走异步（实测点击 <1ms）；`PeriodicSender.apply/start/stop`、`CanFilter`
    硬件滤波、`Replayer.load` 亦有异步版本，统一经 `AsyncExecutor` 后台线程执行，GUI 不再阻塞。
    同步 `sendFrame/sendBatch/apply/start/stop/load` 仍保留供设备测试/CLI。
28. **`QtQuick.Dialogs` 运行时不具备**：RecordPanel 用可编辑 `TextField` 输入路径，未 import 该模块
    （避免风险 2 的运行时导入失败）；如需文件浏览对话框，需先安装运行时模块再 import。
29. **`Replayer` 生命周期**：构造函数启动常驻发送线程，析构时 join（若正卡在设备发送，退出可能等待
    数秒，与 `RxManager::stop()` 最多阻塞 5s 同类）。发送队列无上限（受文件帧数约束）；
    切换倍速以当前已播放时间重新起算以保持进度连续。
30. **位定时 / 时钟（已定论，2026-10-15 实机判决 + 4 速率交叉验证）**：
    - **约定**（ZLG 寄存器约定，已验证）：`bps = clk / ((brp+1) * (tseg1+tseg2+3))`、
      采样点 `= (tseg1+2)/(tseg1+tseg2+3)`。三个字段都是**寄存器值 = 有效值 − 1**；
      `clk` 即真实控制器时钟（V1.01=60MHz、V1.03+=80MHz），**没有"0.8× 实际时钟"**。
    - **证据**：对端 125k 时 `device_bitrate_scan 80 0 1 64 120 3 14` 唯一命中 `brp=39`
      （`80e6/(40*16)=125000`，ACK 5/5）；60MHz 下 `brp=29`（ACK 5/5）。80MHz 交叉验证四档：
      125k→39、250k→19、500k→9、1M→4（形状 tseg1=12/tseg2=1、SP 87.5%），ACK 3/3~5/5。
      旁证：ZLG 官方 demo（clk=60MHz）仲裁 `5/14/3`→`6*20=500k`、数据 `1/10/2`→`2*15=2M`。
    - **同一速率多档并列 ACK 属正常**（SDK 容差内），以「总 Tq=16、SP 87.5%」的标准形状为准。
    - `BitTiming`：`calculate()` / `paramsForTq()` / `totalTq()`；`clockForHardwareVersion()`
      按硬件版本给 60/80MHz；已删除 `canClockForSdkClock()`（0.8×）。
    - **标定表机制已删除**（2026-10-15）：其内置值与公式**完全等价**（80MHz 下 公式算出的
      就是 brp 39/19/9/4），属于冗余状态（还要维护 QSettings 迁移），故连同
      `BitCalibration`、`test_bitcalibration`、界面「已标定」前缀一并移除；遗留的
      QSettings `[calibration]` 组已清理。逃生通道 = 界面「原始位定时（高级，覆盖公式）」。
    - `CanDevice`：`configureChannel()` 一律用公式；`configureChannelRaw()`（原始覆盖）、
      `bitTimingPreview()`、`recommendedClock()`（V1.03+ → 80MHz）。
    - `DeviceController`：读取 `clock`/`useRaw`，`configReport` 暴露实际应用的位定时。
    - `DevicePanel`：时钟下拉（自动 / 60MHz / 80MHz）、原始位定时、每卡显示实际位定时；
      发送失败提示区分「总线无应答」与「设备未打开」。
    - 诊断工具：`device_baud_scan`、`device_bitrate_scan`（模型无关盲扫，判决真实分频用）、
      `device_calibrate <bps>`（实测可用参数并打印，**不落盘**；择优 = ACK → 总 Tq=16 → 帧数，
      零命中记为 0 分）。
    - 单测：`test_bittiming`（新约定 + 实测值 + ZLG demo 值 + `paramsForTq`）。
    - **注意事项**：对端流量阵发、`VCI_Transmit` 的 ACK 判据有抖动，标定/扫描以 ACK 权重更高、
      被动有效帧为辅；总线上的对端速率与本机不一致时，本机可能进入错误态导致发送失败
      （如对端 1M、本机 500k 时 `device_multichannel_test` 的 ch0 会失败）。

