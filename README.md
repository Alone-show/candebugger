# USBCANFD 调试器（CanDebugger）

基于 Qt 5.15 Quick/QML 的 ZLG USBCANFD-200U CAN/CANFD 调试工具，中文界面。
里程碑 1：**设备配置 + 实时接收显示**（步骤 0–3）。
里程碑 2：**单帧发送 + 硬件定时发送**（步骤 4–5）。
里程碑 3：**硬件/软件滤波 + 统计引擎**（步骤 6–7）。
里程碑 4：**实时绘图 + 统计面板**（步骤 8–9）。
里程碑 5：**记录 / 回放**（步骤 10–12）。

## 功能
### 里程碑 1
- 打开/关闭设备，读取 SN、硬件/固件/驱动/API 版本与通道数
- 通道初始化：仲裁域/数据域波特率、采样点、终端电阻、只听模式、合并接收
- 独立接收线程 + 环形缓冲 + 30Hz 合并刷新，界面不卡顿
- 报文列表：时间戳（相对/绝对）、通道、方向、ID、标准/扩展、CAN/FD、DLC、数据
  - 相对时间来自**设备时间戳** `hdr.ts`（实测 1 tick = 100µs，双通道共用，U32 回绕安全）；
    `devTs` 无效时回退主机时间。绝对时间为**主机挂钟**（设备 ts 无 epoch 语义）。
  - 标定探针：`./build/device_ts_probe [帧数] [间隔ms]`（需实机）
- 按 ID 着色、发送/接收着色、自动滚动、暂停、清空、显示/隐藏发送帧

### 里程碑 2
- 单帧发送：标准/扩展、CAN2.0/CANFD、数据/远程、BRS、DLC（0–8 / FD 0–64）
- 数据编辑 Hex/ASCII/十进制；数据生成 CRC-8 / 滚动计数 / 递增填充
- 发送模式（正常/单次/自发自收/自发自收单次）+ 回显；多帧列表批量发送
- 硬件定时发送：最多 8 条，周期(ms)、次数(0=循环)，整体启停与单条启停

### 里程碑 3
- 软件滤波（显示层，即时生效）：ID 单值/范围、方向（全部/仅收/仅发）、
  标准/扩展、CAN/FD/远程/错误；模型级过滤（`FrameFilterProxyModel`），隐藏行不占列表项
- 硬件验收滤波（`CMD_CAN_FILTER`）：ID 起止范围、标准/扩展，可应用/清除
- 统计引擎（每 1s 聚合）：收/发帧率、总线负载率、总帧数、错误帧数、按 ID Top-N

### 里程碑 4
- 零依赖实时绘图 `RealtimePlot`：C++ 自绘 `QQuickItem`（`QSGGeometryNode`）+ 环形缓冲，
  多序列、自动/固定量程、折线与填充、可控线宽
- 统计面板：数值卡片（负载率/收帧率/发帧率/错误帧/总帧数）+ 收/发帧率与
  总线负载率趋势曲线 + 报文 Top-N 列表 + 复位

### 里程碑 5
- 记录器 `Recorder`：后台线程写盘（不阻塞 UI），ASC（CANoe 风格）与 CSV 双格式，可单选/同时写，
  文件名带时间戳；`writtenCount`/`droppedCount`/`errorString`
- 回放器 `Replayer`：自动识别 ASC/CSV，按原始时间间隔或倍速回放；暂停/继续/停止与进度；
  发送在后台线程，GUI 不阻塞
- 记录/回放面板（右侧「记录」页签）：格式勾选、目录、开始/停止、文件加载、倍速、进度条

## 界面布局
深色专业主题（参考 USB-CAN Pro v2.0 设计稿），四区布局：
- 顶部工具条：品牌 + 设备状态徽章（在线/离线、SN、版本）+ 快捷动作
- 左栏「通道配置」：连接/断开设备与信息、CAN1/CAN2 配置卡（仲裁/数据波特率、工作模式、
  终端电阻、采样点）、合并接收开关、应用配置（连接后自动开始接收，改配置后点「应用配置」
  重启通道；连接/配置在后台线程执行，GUI 不阻塞）
- 中栏「实时接收报文」：暂停/清空/保存日志、自动滚动、绝对时间、文本搜索（ID/数据）、
  可折叠滤波区（软件 ID/类型/方向 + 硬件验收滤波）、报文表（序号/时间/通道/方向/ID/
  类型/格式/DLC/数据）、底部状态条（帧率/缓冲/时间格式/监听状态）
- 右栏「统计与总线负载」：RX/TX/ERR/总线状态卡片、负载仪表、负载与帧率趋势
  （`RealtimePlot`）、错误计数与清零、报文 Top-N
- 底部「发送工作区」：单帧发送 / 周期发送 / 记录回放 三个页签

## 线程模型
- **接收**：独立 `RxWorker` 线程 + 环形缓冲 + 30Hz 合并刷新。
- **锁分层**：`CanDevice` 用 `m_rxMutex` 保护接收、`m_ioMutex` 保护发送/配置，二者分离，
  接收线程的阻塞式等待不再饿死 GUI（生命周期调用同时持两锁，与接收串行）。
- **异步执行**：`AsyncExecutor` 后台线程执行发送/周期配置/硬件滤波/回放加载，QML 调用立即返回，
  结果经 `sendCompleted` / `operationFinished` / `loadFinished` / `hardwareApplyFinished` 回传。
- **设备会话**：`DeviceController` 后台执行连接/断开/应用配置，配合「连接中」遮罩与失败弹框。

## 依赖
- Ubuntu/Debian：`qtbase5-dev qtdeclarative5-dev libusb-1.0-0-dev cmake g++`
  - 仅需 QtQuick.Controls 2 **运行时**模块（`libqt5quickcontrols2-5`），无需其 dev 包
- 构建时链接仓库内 `lib/libusbcanfd.so`

## 构建
```sh
cd ui/CanDebugger
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
产物：`build/CanDebugger`（RUNPATH 已设为 `$ORIGIN/lib`，随附 `lib/libusbcanfd.so` 与软链
`libusbcanfd.so.1.0.15`）。

## 运行与 USB 权限
普通用户访问 `3068:0009` 需要 udev 规则：
```sh
sudo cp udev/50-usbcanfd.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
# 重新插拔设备
```
之后直接：
```sh
./build/CanDebugger
```

## 目录结构
```
ui/CanDebugger/
├─ CMakeLists.txt
├─ resources.qrc
├─ include/zcan.h          # ZLG SDK 头文件
├─ lib/                    # libusbcanfd.so (+ .1.0.15 软链)
├─ src/
│  ├─ main.cpp
│  ├─ core/  BitTiming, CanDevice, CanFilter, FrameData, FrameBuilder,
│  │         PeriodicSender, Recorder, Replayer, RingBuffer, RxWorker,
│  │         RxManager, Statistics
│  ├─ model/ FrameListModel, FrameFilterProxyModel
│  ├─ view/  RealtimePlot (QQuickItem), PlotRing
│  └─ qml/   Main, Theme, DevicePanel, ReceiveView, SendWorkspace, SendPanel,
│            PeriodicPanel, StatisticsPanel, RecordPanel,
│            components/ (AppButton, AppCard, AppCheckBox, AppComboBox,
│            AppRadioButton, AppTextField, ChannelBadge, Icon, MetricCard,
│            SectionHeader, SlimScrollBar, StatusDot, ToolbarButton)
├─ tests/  单元：test_bittiming, test_bitcalibration, test_buffer_model,
│          test_framebuilder, test_periodicsender, test_canfilter, test_statistics,
│          test_periodicsender, test_canfilter, test_statistics, test_plotring,
│          test_recorder, test_replayer, test_filter_proxy, test_framelist_roles,
│          test_ring_concurrency；QML：qml/tst_{qml.cpp,panels,runtime}；
│          实机：device_smoke, test_rx_pipeline, device_send_test,
│          device_filter_test, device_merge_test, device_multichannel_test,
│          device_replay_test, device_stress_test
├─ scripts/ run_all_tests.sh, run_hw_tests.sh
├─ TEST_PLAN.md
└─ udev/50-usbcanfd.rules
```

## 位定时说明（重要）
**实测标定出的 ZLG 寄存器约定**（已实机判决 + 4 速率交叉验证）：

```
baud      = clk / ((brp + 1) * (tseg1 + tseg2 + 3))
采样点(%) = 100 * (tseg1 + 2) / (tseg1 + tseg2 + 3)
```

- `brp / tseg1 / tseg2` 都是**寄存器值 = 有效值 − 1**（SDK 各自 +1）；`clk` 就是
  **真实控制器时钟**：V1.01 = 60MHz、V1.03+ = 80MHz（界面「自动」按硬件版本推断）。
- 内置实测值（USBCANFD-200U HW=0x0300，80MHz + 形状 `tseg1=12 tseg2=1`、SP 87.5%）：

  | 速率 | brp | tseg1 | tseg2 | 实测 |
  |---|---|---|---|---|
  | 125k | 39 | 12 | 1 | ACK 5/5 |
  | 250k | 19 | 12 | 1 | ACK 3/3 |
  | 500k | 9 | 12 | 1 | ACK 3/3 |
  | 1M | 4 | 12 | 1 | ACK 3/3 |

- 「标定表」（`BitCalibration`，QSettings）**优先于公式**，用于固定实测值/现场标定/手工覆盖；
  未标定的速率直接用公式（公式在 60/80MHz 下已能精确算出 125k~5M）。
- 每张通道卡显示**实际使用的位定时**并标注「已标定 / 公式」；连接与「应用配置」
  后的实际参数显示在左栏底部。
- 标定表持久化在 `~/.config/CanDebugger/USBCANFD 调试器.conf`（QSettings）。
- **新增速率标定**：`./build/device_calibrate <bps> [通道]`（需实机 + 对端在该速率上发送），
  实测选出可用参数并写入标定表，App 重启即生效。
- 高级：勾选「原始位定时（高级，覆盖公式）」可直接填写 `brp/tseg1/tseg2/sjw`，绕过标定与公式。
- 排查工具：
  - `./build/device_baud_scan [bps] [采样点%] [通道]`：按标准速率找位定时（被动监听 + ACK）。
  - `./build/device_bitrate_scan [时钟MHz] [通道] [最小] [最大] [监听ms] [探针] [固定tq]`：
    **模型无关**朴素扫描，用于未知时钟/未知速率时定位真实位定时。

## 测试
详细测试计划与执行报告见 `TEST_PLAN.md`。

```sh
# L1 单元 + L2 QML（无需设备）
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure      # bittiming/buffer_model/framebuilder/
                                                # periodicsender/canfilter/statistics/plotring/
                                                # recorder/replayer/filter_proxy/
                                                # framelist_roles/ring_concurrency/
                                                # bitcalibration/qml

# L4/L5 健壮性与覆盖率（脚本封装）
scripts/run_all_tests.sh release                # 纯逻辑 + QML
scripts/run_all_tests.sh asan                   # AddressSanitizer + UBSan
scripts/run_all_tests.sh tsan                   # ThreadSanitizer
scripts/run_all_tests.sh coverage               # gcov/lcov 覆盖率报告
scripts/run_all_tests.sh all                    # 全部

# L3 实机（需设备权限，串行编排）
scripts/run_hw_tests.sh
```

实机测试可单独运行（均需 USB 权限）：
```sh
./build/device_smoke              # 设备打开/配置/自发自收
./build/test_rx_pipeline          # 设备→线程→环形缓冲→模型 全链路
./build/device_send_test          # 单帧发送 + 硬件定时发送
./build/device_filter_test        # 硬件验收滤波 + 软件模型滤波 + 统计联动
./build/device_merge_test         # 合并接收模式
./build/device_multichannel_test  # 双通道并发收发
./build/device_replay_test        # 记录→回放 ASC/CSV 回环
./build/device_stress_test        # ~1000fps 压力与内存
./build/device_baud_scan 250000   # 位定时扫描（标准速率 + 监听/ACK）
./build/device_bitrate_scan       # 模型无关朴素扫描（未知时钟/速率定位真实分频）
./build/device_calibrate 125000   # 实测标定并写入标定表（App 共用）
```


## 滤波与统计说明
- 软件滤波：`rx.filter` 属性，模型级过滤（代理），修改即时生效，不改变 SDK 上报量。
- 硬件滤波：`rx.filter.applyHardware(port, ranges)` / `clearHardware(port)`；
  `ZCAN_FILTER_TABLE.size` 取字节数 `sizeof(ZCAN_FILTER)*n`；验收滤波仅作用于总线接收帧，
  本机发送回环（`tx=1`）不受影响。
- 统计：`statistics` 上下文属性，每 100ms 聚合；接收帧按批注入（无逐帧信号），
  发送计数由 `FrameBuilder` 累计；负载率为位长估算值。

## 绘图与统计面板说明
- `RealtimePlot` 为零依赖自绘 `QQuickItem`（`src/view/RealtimePlot.{h,cpp}`），QML 中
  `import CanDebugger 1.0` 后直接使用；数据经 `append(series, value)` 追加到固定容量环形缓冲，
  `clear()` 清空。属性：`series`、`maxPoints`、`autoScale`、`fill`、`lineWidth`、
  `lineColors`、`minValue`、`maxValue`。
- 线程安全：环形缓冲由互斥锁保护，`updatePaintNode()` 只在锁内做快照，几何构建在锁外，
  `append()` 只触发合并重绘（不逐点重绘、不新增逐帧信号）。
- 统计面板（右侧「统计」页签）：卡片显示负载率/收帧率/发帧率/错误帧/总帧数；
  两个趋势图分别显示收/发帧率与总线负载率（0–100%）；下方为报文 Top-N 列表与「复位」按钮。

## 发送说明
- 帧字段由 `FrameBuilder` 构建，DLC 为准（数据不足补零、超出截断）；远程帧不带数据。
- 数据生成：CRC-8（poly 0x07，写末字节）、滚动计数（写首字节）、递增填充（从 0 起），
  为本地约定，跨节点使用需与对端统一。
- 定时发送经 `CMD_CAN_TTX`/`CMD_CAN_TTX_CTL` 下发硬件表，`ZCAN_TTX_CFG.size` 取字节数；
  `interval` 单位 100us（UI 以 ms 输入）。
- 发送模式默认「正常」。`VCI_Transmit` 正常模式会等待总线 ACK，无应答总线上默认等到 ~1.5s；
  `CanDevice::startChannel()` 会下发 `CMD_CAN_TX_TIMEOUT(0x44)`，等待上限由 `txTimeoutMs`
  控制（默认 20ms，可用 `canDevice.txTimeoutMs` 调整），超时后帧仍会提交。
- 发送失败提示：返回失败时提示「总线无应答（请检查波特率/位定时、接线与终端电阻）」，
  设备未打开则提示「设备未打开」——便于区分是**位定时不匹配**还是**设备状态问题**。

## 记录 / 回放说明
- 记录接入：接收帧在 `RxManager::drain()` 中按批非阻塞入队（**显示层软件滤波之前**，
  不受显示过滤影响），本机发送由 `FrameBuilder` 成功发送路径记录；接收侧的 `tx` 帧
  （回显/回环）被跳过以避免重复。**硬件定时发送（`CMD_CAN_TTX`）与回放发送不计入记录**。
- 格式：ASC（CANoe 风格，`时间 通道 id[x] 方向 kind dlc data`，`kind ∈ d/r/fd/fdb/fdr/e`）；
  CSV（表头 `timestamp,channel,dir,id,type,dlc,data`）。时间为相对记录起点的秒（6 位小数）。
- 回放：自动识别 ASC/CSV，`Replayer::load()` 解析后 `play()`；`speed` 倍速、`pause()/resume()/stop()`，
  进度 `progress`（0–100）。**发送在独立后台线程**（设备发送调用可能阻塞数秒），GUI 不阻塞。
- QML：`recorder`/`replayer` 上下文属性；右侧「记录」页签操作。
  `QtQuick.Dialogs` 运行时不具备，故文件路径用可编辑文本框输入。
