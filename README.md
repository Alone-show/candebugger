# USBCANFD 调试器（CanDebugger）

基于 Qt 5.15 Quick/QML 的 ZLG USBCANFD-200U CAN/CANFD 调试工具，中文界面。
里程碑 1：**设备配置 + 实时接收显示**（步骤 0–3）。
里程碑 2：**单帧发送 + 定时发送**（步骤 4–5；步骤 14 起定时发送改为软件实现）。
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
- 数据编辑 Hex/ASCII/十进制；数据生成 CRC-8 / 滚动计数 / 递增填充 / ID 递增
- 发送模式（正常/单次/自发自收/自发自收单次）+ 回显；**快捷周期发送**（把当前单帧按
  周期(ms)/次数(默认 1；0=循环)反复发出，控件就在「发送」同一行；独立于周期页签的配置表，
  互不干扰；数据生成随之逐帧生效）
- 周期发送：软件定时（PC 侧精确定时线程），**条数不限**，周期(ms)、次数(0=循环)；
  两通道以内联标签页分别配置与独立启停；同一 tick 到期的帧按 CAN / CAN-FD 分组合并为
  一次 USB 调用提交（每批最多 2 次调用），避免逐帧一次 USB 往返把表内相邻帧拉开

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
深色专业主题（参考 USB-CAN Pro v2.0 设计稿），支持 **深色 / 浅色** 两套色板（标题栏右侧太阳/月亮按钮一键切换，
选择持久化到 `~/.config/CanDebugger/`），四区布局：
- 顶部标题栏（**无边框自绘**，一行到底）：品牌 + 版本 + 设备状态徽章（在线/离线、SN、版本）
  + 记录中/帮助/设置，最右侧为最小化 / 最大化-还原 / 关闭；标题栏空白处可拖动窗口、
  双击最大化-还原，窗口四边四角 5px 热区可缩放
- 左栏「通道配置」：连接/断开设备与信息、CAN1/CAN2 配置卡（仲裁/数据波特率、工作模式、
  终端电阻、采样点）、合并接收开关、应用配置（连接后自动开始接收，改配置后点「应用配置」
  重启通道；连接/配置在后台线程执行，GUI 不阻塞）
- 中栏「实时接收报文」：暂停/清空/保存日志、自动滚动、绝对时间、文本搜索（ID/数据）、
  可折叠滤波区（软件 ID/类型/方向 + 硬件验收滤波）、报文表（序号/时间/通道/方向/ID/
  类型/格式/DLC/数据）、底部状态条（帧率/缓冲/时间格式/监听状态）
- 右栏「统计与总线负载」：RX/TX/ERR/总线状态卡片、负载仪表、负载与帧率趋势
  （`RealtimePlot`）、错误计数与清零、报文 Top-N
- 底部「发送工作区」：单帧发送 / 周期发送 / 记录回放 三个页签

### 无边框标题栏的拖动 / 缩放（按后端分流）
标题栏用 `Qt.FramelessWindowHint` 去掉系统装饰后，拖动与缩放按窗口后端走两条路
（后端名由 `main.cpp` 注入的 `platformName` = `QGuiApplication::platformName()` 区分）：
- **Wayland**：调用 QML `Window.startSystemMove()` / `startSystemResize(edges)` 交给合成器，
  吸附、平铺、跨屏、解除最大化等都由窗口管理器完成（Wayland 下客户端本来就无法设置窗口位置）。
- **X11（xcb）**：Qt 5.15 的实现只是往根窗口发一条 `_NET_WM_MOVERESIZE` 客户端消息，
  然后**无条件返回 `true`**；实测 KWin 会忽略由运动事件触发的移动请求（调用成功但窗口不动），
  因此 X11 下改为 QML 自己计算位置与尺寸（`window.x/y/width/height`），不依赖窗口管理器。
- 拖动有 4px 阈值（`theme.dragThreshold`）：阈值内不启动拖动，保证单击/双击最大化可用。

## 线程模型
- **接收**：独立 `RxWorker` 线程 + 环形缓冲 + 30Hz 合并刷新。
- **锁分层**：`CanDevice` 用 `m_rxMutex` 保护接收、`m_ioMutex` 保护发送/配置，二者分离，
  接收线程的阻塞式等待不再饿死 GUI（生命周期调用同时持两锁，与接收串行）。
- **异步执行**：`AsyncExecutor` 后台线程执行发送/硬件滤波/回放加载，QML 调用立即返回，
  结果经 `sendCompleted` / `operationFinished` / `loadFinished` / `hardwareApplyFinished` 回传。
  （周期发送为软件实现，`apply/start/stop` 为同步快路径，结果仍经 `operationFinished` 通知。）
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

## 打包成 .deb
```sh
scripts/package_deb.sh                 # 自动 Release 构建（build-release/）并出包
MAINTAINER="Your Name <you@example.com>" scripts/package_deb.sh
BUILD_DIR=build scripts/package_deb.sh # 用已有构建目录
```
产物：`dist/candebugger_<版本>_<架构>.deb`（版本取自 `CMakeLists.txt` 的 `project(... VERSION)`）。
安装 / 卸载：
```sh
sudo dpkg -i dist/candebugger_0.1.1_amd64.deb
sudo dpkg -r candebugger
```

**自包含**：包内自带 Qt5 运行库（`libQt5*.so`）、**ICU**（`libicu*.so`）、平台插件（xcb /
wayland / offscreen）与 QtQuick/QML 模块，**不依赖目标机安装 Qt**；`libusbcanfd.so` 也一并随附。
安装布局：

| 路径 | 内容 |
|---|---|
| `/opt/candebugger/bin/CanDebugger` | 主程序（`qt.conf` / `qtquickcontrols2.conf` 同目录） |
| `/opt/candebugger/lib/` | 自带 Qt 运行库 + ICU + `libusbcanfd.so.1.0.15` |
| `/opt/candebugger/plugins/`、`/opt/candebugger/qml/` | Qt 平台插件、QtQuick/QML 模块 |
| `/usr/bin/candebugger` | 启动包装脚本（设置库 / 插件 / QML 搜索路径后 exec） |
| `/usr/share/applications/candebugger.desktop`、图标 | 桌面菜单项 |
| `/usr/lib/udev/rules.d/50-usbcanfd.rules` | 设备访问规则（postinst 自动 `udevadm reload/trigger`） |

体积：deb ≈ 18 MB / 安装后 ≈ 72 MB（ICU 的 `libicudata.so.76` 一项就占 30 MB 未压缩）。

要点：
- **ICU 必须自带（不能省）**：发行版的 `libQt5Core.so.5` 的 DT_NEEDED 里直接列了
  `libicui18n.so.76` / `libicuuc.so.76`，而且 ICU 的符号是**带版本后缀**的（`u_strToUpper_76`、
  `ucol_open_76`…），所以既不能指向别的 ICU 大版本，也不能指望"不走到就不崩"——`Qt5Core`
  在 `main` 之前就调用了它。实测用空壳 ICU 库替换会直接
  `symbol lookup error: libQt5Core.so.5: undefined symbol: u_strToUpper_76`。
  这就是此前"在 25.10 打包、拿到 26.04 装"失败的原因（那边只有 ICU 74/78）。
- **`Depends` 取「直接 DT_NEEDED」而非 `ldd` 全传递闭包**：依赖表更短更稳，不会把
  ICU / DBus 的深层传递依赖（`libkrb5-*`、`libgssapi-krb5-2` 之类）写进来；包内自带的库
  （Qt5 / ICU / libusbcanfd）自动不算依赖，但仍会检查它们自己的 NEEDED。
- **跨发行版别名**：只改包名、不改 SONAME 的条目写成 alternatives ——
  `libglib2.0-0 | libglib2.0-0t64`、`libpng16-16 | libpng16-16t64`、`libffi8 | libffi7`。
  `libicu*` 不应出现在 `Depends` 里（出现会告警）。
- **架构过滤**：解析 `ldconfig -p` 时只取本机架构的条目（`ldconfig` 在启用 i386 的机器上会同时
  列出 32 位库，否则 `libstdc++.so.6` 会被解析成 `lib32stdc++6`）。
- **去符号**：打包时对包内所有 ELF 执行 `strip --strip-unneeded`（保留 `.dynsym`，动态加载不受影响）。
  实测 Ubuntu 自带的 Qt 库 / 插件本就已 strip（无 `.symtab`），因此只有主程序与厂商 SDK 库
  真正变小：`CanDebugger` 717K→572K、`libusbcanfd.so` 254K→224K，安装体积 −170 KB、
  **deb 文件 −28 KB**（符号表压缩率高）。想要更大空间需换别的办法，见下。
  可用 `STRIP=0` 关闭。
- **压缩**：默认用 `-Zxz -z9`（dpkg-deb 1.22 自带默认是 zstd -19，xz -9 大约小 9%）。
  换回默认：`DEB_COMPRESS="-Zzstd -z19" scripts/package_deb.sh`。
- 强制使用 Qt Quick Controls 2 内置 **Default** 样式（`qtquickcontrols2.conf`），避免在 KDE 桌面
  被切到 `org.kde.desktop` 样式而多依赖 libKF5*。
- 不打 `imageformats` / `iconengines`（程序不使用 `QIcon`/QML `Image`；带上会把 `libQt5Svg` →
  `libQt5Widgets` 及 KDE 的 `KIconEnginePlugin` 拖进来）。

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

### 连接失败时的原因判定
`VCI_OpenDevice` 只返回成功/失败，不区分原因。因此「连接设备」失败时会用
`UsbProbe`（只读 `/sys/bus/usb/devices` 与 `/proc/<pid>/fd`，**不打开设备**）判定真实原因，
并只给出对应的那一条修复动作：

| 判定 | 含义 | 提示的修复动作 |
|---|---|---|
| 未检测到设备 | sysfs 里没有 `3068:0009` | 检查 USB 线缆与供电，重新插拔 |
| 型号不符 | 有 `3068:xxxx` 但 PID 不是 `0009` | 换用受支持的型号 |
| 无权限访问 | 设备存在，但设备节点不可读写（含 devtmpfs 未挂载） | 安装上面的 udev 规则后重新插拔 |
| 设备被占用 | 节点可访问，但被其它进程持有 fd | 关闭提示中列出的进程（会点名 `进程名(pid)`） |
| 已检测到但打开失败 | 设备在位、可访问、无人占用 | 固件/驱动未就绪，重新插拔或重启 |
| 无法判定 | 非 Linux / sysfs 不可用 | 退回通用提示 |

探测只在打开**失败后**执行，成功路径没有任何额外开销。实现见 `src/core/UsbProbe.{h,cpp}`，
判定矩阵的单测见 `tests/test_usbprobe.cpp`。

## 目录结构
```
ui/CanDebugger/
├─ CMakeLists.txt
├─ resources.qrc
├─ include/zcan.h          # ZLG SDK 头文件
├─ lib/                    # libusbcanfd.so (+ .1.0.15 软链)
├─ packaging/              # .deb 打包素材：control.in / postinst / postrm /
│  │                       #   candebugger(包装脚本) / *.desktop / qt.conf /
│  │                       #   qtquickcontrols2.conf / make_icon.py
├─ dist/                   # .deb 产物（忽略提交）
├─ src/
│  ├─ main.cpp
│  ├─ core/  BitTiming, CanDevice, CanFilter, FrameData, FrameBuilder,
│  │         PeriodicSender, Recorder, Replayer, RingBuffer, RxWorker,
│  │         RxManager, Statistics, UsbProbe
│  ├─ model/ FrameListModel, FrameFilterProxyModel
│  ├─ view/  RealtimePlot (QQuickItem), PlotRing
│  └─ qml/   Main, Theme, DevicePanel, ReceiveView, SendWorkspace, SendPanel,
│            PeriodicPanel, StatisticsPanel, RecordPanel,
│            components/ (AppButton, AppCard, AppCheckBox, AppComboBox,
│            AppRadioButton, AppTextField, ChannelBadge, ChannelTab, Icon, MetricCard,
│            SectionHeader, SlimScrollBar, StatusDot, ToolbarButton,
│            WindowButton)
├─ tests/  单元：test_bittiming, test_buffer_model, test_framebuilder,
│          test_periodicsender, test_canfilter, test_statistics,
│          test_periodicsender, test_canfilter, test_statistics, test_plotring,
│          test_recorder, test_replayer, test_filter_proxy, test_framelist_roles,
│          test_ring_concurrency, test_usbprobe；QML：qml/tst_{qml.cpp,panels,runtime}；
│          实机：device_smoke, test_rx_pipeline, device_send_test,
│          device_periodic_test, device_filter_test, device_merge_test,
│          device_multichannel_test, device_replay_test, device_stress_test
│          工具：device_baud_scan/device_bitrate_scan/device_calibrate
├─ scripts/ run_all_tests.sh, run_hw_tests.sh, package_deb.sh
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
- 公式在 60/80MHz 下都能**精确**算出常用速率（125k~5M）。以 80MHz + SP 87.5% 为例：

  | 速率 | brp | tseg1 | tseg2 | 实测 |
  |---|---|---|---|---|
  | 125k | 39 | 12 | 1 | ACK 5/5 |
  | 250k | 19 | 12 | 1 | ACK 3/3 |
  | 500k | 9 | 12 | 1 | ACK 3/3 |
  | 1M | 4 | 12 | 1 | ACK 3/3 |

- 每张通道卡显示**实际使用的位定时**；连接与「应用配置」后的实际参数显示在左栏底部。
- 高级：勾选「原始位定时（高级，覆盖公式）」可直接填写 `brp/tseg1/tseg2/sjw`，
  用于非标准速率或特殊硬件（绕过公式）。
- **非标准速率现场诊断**：`./build/device_calibrate <bps> [通道]`（需实机 + 对端在该速率上发送）
  实测哪组参数真的能通并打印出来（**不落盘**），可直接填到上面的「原始位定时」里。
- 排查工具：
  - `./build/device_baud_scan [bps] [采样点%] [通道]`：按标准速率找位定时（被动监听 + ACK）。
  - `./build/device_bitrate_scan [时钟MHz] [通道] [最小] [最大] [监听ms] [探针] [固定tq]`：
    模型无关朴素扫描，用于未知时钟/未知速率时定位真实分频（判决约定时用的就是它）。

## 测试
详细测试计划与执行报告见 `TEST_PLAN.md`。

```sh
# L1 单元 + L2 QML（无需设备）
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure      # bittiming/buffer_model/framebuilder/
                                                # periodicsender/canfilter/statistics/plotring/
                                                # recorder/replayer/filter_proxy/
                                                # framelist_roles/ring_concurrency/qml

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
./build/device_send_test          # 单帧发送 + 软件定时发送
./build/device_periodic_test      # 周期表批量提交：同 tick 相邻帧总线级间隔（[条数] [周期ms] [运行ms]）
./build/device_filter_test        # 硬件验收滤波 + 软件模型滤波 + 统计联动
./build/device_merge_test         # 合并接收模式
./build/device_multichannel_test  # 双通道并发收发
./build/device_replay_test        # 记录→回放 ASC/CSV 回环
./build/device_stress_test        # ~1000fps 压力与内存
./build/device_baud_scan 250000   # 位定时扫描（标准速率 + 监听/ACK）
./build/device_bitrate_scan       # 模型无关朴素扫描（未知时钟/速率定位真实分频）
./build/device_calibrate 125000   # 实测诊断：打印可用参数（不落盘）
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
- 数据生成：CRC-8（poly 0x07，写末字节）、滚动计数（写首字节）、递增填充（数据从 0 起）、
  ID 递增（ID = 基址 + 计数器，标准帧按 0x7FF / 扩展帧按 0x1FFFFFFF 回绕；与滚动计数共用
  同一计数器，二者任一勾选即逐帧 +1），为本地约定，跨节点使用需与对端统一。
- 定时发送为**软件实现**：`PeriodicSender` 用单调时钟调度 + 独立发送线程，经
  `transmitCan`/`transmitFd` 逐帧发出（不再使用硬件定时表 `CMD_CAN_TTX`），因此条数不限；
  周期最小 1ms（UI 以 ms 输入）；落后超过一个周期时丢弃落后时隙并重新对齐，不突发补发。
- 发送模式默认「正常」。`VCI_Transmit` 正常模式会等待总线 ACK，无应答总线上默认等到 ~1.5s；
  `CanDevice::startChannel()` 会下发 `CMD_CAN_TX_TIMEOUT(0x44)`，等待上限由 `txTimeoutMs`
  控制（默认 20ms，可用 `canDevice.txTimeoutMs` 调整），超时后帧仍会提交。
- 发送失败提示：返回失败时提示「总线无应答（请检查波特率/位定时、接线与终端电阻）」，
  设备未打开则提示「设备未打开」——便于区分是**位定时不匹配**还是**设备状态问题**。

## 记录 / 回放说明
- 记录接入：接收帧在 `RxManager::drain()` 中按批非阻塞入队（**显示层软件滤波之前**，
  不受显示过滤影响），本机发送由发送路径记录（`FrameBuilder` 单帧/批量、`PeriodicSender`
  软件定时）；接收侧的 `tx` 帧（回显/回环）被跳过以避免重复。**回放发送不计入记录**。
- 格式：ASC（CANoe 风格，`时间 通道 id[x] 方向 kind dlc data`，`kind ∈ d/r/fd/fdb/fdr/e`）；
  CSV（表头 `timestamp,channel,dir,id,type,dlc,data`）。时间为相对记录起点的秒（6 位小数）。
- 回放：自动识别 ASC/CSV，`Replayer::load()` 解析后 `play()`；`speed` 倍速、`pause()/resume()/stop()`，
  进度 `progress`（0–100）。**发送在独立后台线程**（设备发送调用可能阻塞数秒），GUI 不阻塞。
- QML：`recorder`/`replayer` 上下文属性；右侧「记录」页签操作。
  `QtQuick.Dialogs` 运行时不具备，故文件路径用可编辑文本框输入。
