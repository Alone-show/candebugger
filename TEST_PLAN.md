# CanDebugger 详细测试计划与执行报告

- 版本：第一版（步骤 0–12 功能完成）
- 日期：2026-09-24
- 工程：`ui/CanDebugger/`
- 设备：ZLG USBCANFD-200U（`3068:0009`，双通道）

---

## 1. 目标与范围

对第一版全部功能做分层验证：设备配置 / 实时接收 / 单帧与定时发送 / 硬件与软件滤波 /
统计 / 实时绘图 / 记录与回放。产出：**测试计划 + 自动化测试代码 + 可复现脚本 + 结果报告**。

约束（已确认）：
- 仅有本机自发自收 / 双通道短接条件，**无外部 CAN 节点** → 互操作性测试列为遗留项
- 纳入 QML 自动化、压力、ASan/TSan、覆盖率

---

## 2. 测试环境

| 项 | 版本/状态 |
|---|---|
| OS | Ubuntu 25.10 (questing) |
| 编译器 | GCC 15 / 构建 14.3 |
| CMake | 3.31.6 |
| Qt | 5.15.17（Core/Gui/Qml/Quick/Test/QuickTest） |
| 测试工具 | ctest、QtTest、QtQuickTest、ASan/UBSan、TSan、lcov 2.3、cppcheck、clang-tidy |
| SDK | `lib/libusbcanfd.so`（SONAME `.1.0.15`） |
| udev | 已安装，设备节点普通用户可访问 |
| 新增依赖 | `qml-module-qttest`、`lcov`、`cppcheck`（apt 安装） |

---

## 3. 分层策略

```
L1 单元/纯逻辑   ctest（无设备）
L2 QML 组件测试  QtQuickTest + offscreen
L3 实机集成      需设备（自收 / 双通道）
L4 健壮性         ASan/UBSan、TSan、压力/内存
L5 覆盖率         gcov/lcov
L6 静态分析       cppcheck（+ clang-tidy）
L7 人工验收       部署、热插拔、外部节点互操作
```

---

## 4. 用例清单

### L1 单元测试（`ctest`，无设备）

| 用例 | 覆盖点 |
|---|---|
| `test_bittiming` | 标准预设、80MHz 时钟、任意速率近似、maxTq 钳制、采样点目标与钳制、退化参数 |
| `test_buffer_model` | 环形缓冲顺序/溢出/批量、模型角色、裁剪、清空、时间戳 |
| `test_framebuilder` | Hex/ASCII/Dec、CRC-8、ID 掩码、CAN/FD 字段、远程、补零/截断、**DLC 边界与钳制**、**空缓冲拒绝、CAN 拒绝 FD 标志**、**修饰符组合**、**data 转换**、spec 往返 |
| `test_periodicsender` | 周期换算、字段/索引/flags、FD、>8 拒绝、空表、非法数据 |
| `test_canfilter` | ID 范围/单值、标准 vs 扩展掩码、方向、类型、格式、硬件表字节数/截断交换/超 64 组 |
| `test_statistics` | fps 公式、经典/FD 位长、rx/错误计数、发送计数、窗口速率、负载率、Top-N 排序、**边界与钳制** |
| `test_plotring` | 基本读写、溢出保留最新、多序列、增删、改容量、清空 |
| `test_recorder` | ASC/CSV 往返、写盘往返、tx 跳过、溢出守恒、幂等、**畸形行、类型变体、扩展后缀、header/时间辅助** |
| `test_replayer` | 格式识别、解析、归一化、进度/倍速、调度、文件加载、闭环、**未知格式/空帧/边界** |
| `test_filter_proxy` | **新增**：默认放行、ID/方向/格式过滤、代理映射、动态重过滤、源清空、count 信号 |
| `test_framelist_roles` | **新增**：13 个角色完整性、全角色取值、格式/方向文本、ID 掩码、着色、越界、maxRows、时间基准、count 信号 |
| `test_ring_concurrency` | **新增**：单/多生产者-消费者不变式、并发 vector 收发（TSan 目标） |

### L2 QML 测试（`tst_qml`，offscreen）

| 用例 | 覆盖点 |
|---|---|
| `Panels::test_allQmlCompile` | 8 个 QML 文件全部编译通过 |
| `Panels::test_instantiatePanels` | 6 个面板实例化/销毁无错误（注入 `theme`） |
| `Runtime::*` | 7 个上下文属性存在、rx 暂停/时间戳/清空、软件滤波属性、统计属性、FrameBuilder 转换、Recorder/Replayer 属性、`import CanDebugger 1.0`（RealtimePlot 注册） |

### L3 实机测试（`scripts/run_hw_tests.sh`，需设备）

| 用例 | 覆盖点 |
|---|---|
| `device_smoke` | 打开/信息/通道/自发自收 |
| `test_rx_pipeline` | 设备→线程→环形缓冲→模型 全链路 |
| `device_send_test` | 单帧 CAN/FD + 硬件定时发送 + 单条启停 |
| `device_filter_test` | 硬件验收滤波 + 软件模型滤波 + 统计联动 |
| `device_merge_test` | **新增**：合并接收 `VCI_ReceiveData` 全链路 + 取消合并恢复 |
| `device_multichannel_test` | **新增**：双通道并发收发与通道归属 |
| `device_replay_test` | 记录→回放 ASC/CSV 回环 |
| `device_stress_test` | **新增**：~1000fps 持续、丢弃率、内存增长 |

### L4/L5/L6

- ASan/UBSan：全部 L1+L2 用例（`detect_leaks=0`，避开 Qt 已知误报）
- TSan：`ring_concurrency` + `filter_proxy` + `buffer_model`（无设备，缩小规模）
- 覆盖率：`build-cov` + lcov，L1+L2
- 静态分析：cppcheck `--enable=warning,performance,portability --library=qt`

---

## 5. 新增/改动文件

```
CanDebugger/
├─ CMakeLists.txt                         # + ASan/TSan/覆盖选项、3 新单测、tst_qml、3 实机测试
├─ scripts/run_all_tests.sh               # L1/L2/L4/L5 编排（release|asan|tsan|coverage|all）
├─ scripts/run_hw_tests.sh                # L3 实机串行编排
├─ tests/
│  ├─ test_filter_proxy.cpp               # 新增
│  ├─ test_framelist_roles.cpp            # 新增
│  ├─ test_ring_concurrency.cpp           # 新增
│  ├─ device_merge_test.cpp               # 新增
│  ├─ device_multichannel_test.cpp        # 新增
│  ├─ device_stress_test.cpp              # 新增
│  ├─ qml/tst_qml.cpp, tst_panels.qml, tst_runtime.qml   # 新增
│  └─ test_{bittiming,framebuilder,recorder,replayer,statistics}.cpp  # 补强
├─ src/core/RingBuffer.{h,cpp}            # QMutex → std::mutex（见 第 8 节 发现 1）
└─ TEST_PLAN.md                           # 本文档
```

---

## 6. 执行方式

```sh
# L1 + L2
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure

# L4/L5（脚本封装）
scripts/run_all_tests.sh release     # 纯逻辑 + QML
scripts/run_all_tests.sh asan        # ASan/UBSan
scripts/run_all_tests.sh tsan        # TSan
scripts/run_all_tests.sh coverage    # gcov/lcov 报告
scripts/run_all_tests.sh all         # 全部

# L3（需设备）
scripts/run_hw_tests.sh
```

---

## 7. 执行结果（2026-09-24）

| 层级 | 结果 | 关键数据 |
|---|---|---|
| L1+L2 `ctest` | ✅ **13/13 通过** | 总耗时 0.22s |
| QML `tst_qml` | ✅ **13/13 通过** | 182ms，无 QML 警告 |
| ASan/UBSan | ✅ **13/13 通过** | 无越界/UAF/UB（leak 检测关闭） |
| TSan | ✅ **3/3 通过** | 0.16s，0 数据竞争 |
| 覆盖率（L1+L2） | ℹ️ **60.9% 行 / 70.3% 函数** | 15 个源文件 |
| cppcheck | ℹ️ **0 错误/警告**，8 条性能建议 | 均为 QString getter 返回值 |
| L3 实机 | ✅ **8/8 通过** | 见下 |

覆盖率明细（行覆盖）：

```
core/BitTiming.cpp              88.9%      model/FrameFilterProxyModel.cpp  95.0%
core/CanFilter.cpp              70.0%      model/FrameListModel.cpp         86.4%
core/FrameBuilder.cpp           76.6%      view/PlotRing.h                  92.7%
core/Recorder.cpp               91.9%      core/RingBuffer.cpp              84.5%
core/Statistics.cpp             91.3%      core/Replayer.cpp                57.6%
core/RxManager.cpp              48.2%      core/PeriodicSender.cpp          37.2%
core/CanDevice.cpp               8.2%      core/RxWorker.cpp                 0.0%
view/RealtimePlot.cpp           20.0%
```

> 说明：`CanDevice`/`RxWorker`/`RxManager`（设备相关）与 `RealtimePlot`（场景图）覆盖率低，
> 因为覆盖率只跑了免设备的 L1+L2；L3 实机用例未计入 gcov。

实机结果：

| 用例 | 结果 | 关键数据 |
|---|---|---|
| device_smoke | ✅ 2s | SN、双通道、自发自收 2 |
| test_rx_pipeline | ✅ 2s | 接收 100 / 丢弃 0 / 模型 100 |
| device_send_test | ✅ 14s | CAN+FD 自收；定时 700ms 增 28 帧 |
| device_filter_test | ✅ 53s | 硬件仅 0x200 生效；软件仅 0x300；统计联动 |
| device_merge_test | ✅ 16s | 合并收 CAN/FD 成功；取消合并恢复 |
| device_multichannel_test | ✅ 3s | ch0/ch1 并发，通道归属正确 |
| device_replay_test | ✅ 12s | ASC/CSV 记录→回放自收一致 |
| device_stress_test | ✅ 45s | **~1226–1316 fps，丢弃 0，RSS +1.2MB/8s** |

---

## 8. 测试发现

1. **Qt 5.15 `QMutex` 的 TSan 误报（已处理）**
   Qt 5.15 的 `QMutex` 为 futex 实现，TSan 无法建立 happens-before，
   导致 `RingBuffer` 在 `m_head/m_count` 上被误报数据竞争。
   已将 `RingBuffer` 改用 `std::mutex`（语义等价、TSan 可追踪）；
   改后 TSan 由 254s + 55 警告变为 **0.16s + 0 警告**。其余模块仍用 `QMutex`。
2. **硬件定时发送最小间隔**：8 条 `CMD_CAN_TTX` 在 2–10ms 区间实测吞吐稳定在
   ~1000–1300 fps（设备/总线上限），可满足 1000fps 验收；首次运行偶发无流量，
   已加入「通道复位 + 一次重试」保证稳定。
3. **`VCI_Transmit` 并发阻塞（风险 27 复现）**：实机发送偶发阻塞数秒（`device_send_test`
   端到端 ~14–53s），属已知问题，非本次测试引入。

---

## 9. 退出准则与遗留项

**已满足**：L1–L4 全绿 + 无 sanitizer 报告 + 静态分析无错误。

**遗留 / 阻塞项（明确标注，不阻塞第一版验收）**：
- ❗ **外部节点互操作性**（最高优先级风险 1）：无硬件，未验证标准公式 500k/2M 与第三方节点互通
- ⚠️ `VCI_Transmit` 在接收并发时阻塞 → `FrameBuilder` 仍在 GUI 线程同步发送（风险 27）
- ⚠️ 硬件定时发送（`CMD_CAN_TTX`）与回放发送不计入 `Statistics.txCount` 与记录（风险 17/23）
- ⚠️ `hdr.ts` 设备时间戳单位未确认（风险 5）
- ℹ️ 设备相关文件覆盖率偏低（需将 L3 纳入 gcov 才能提升）
- ℹ️ 8 条 cppcheck 性能建议（QString getter 按值返回，Q_PROPERTY 习惯写法，可不改）

---

## 10. 后续建议

1. 接入外部 CAN 节点，补充互操作性用例（仲裁、错误帧、总线负载标定）
2. 为 `CanDevice` 增加发送队列/异步发送，消除 GUI 线程阻塞（对应风险 27）
3. 将 L3 实机测试纳入覆盖率（gcov 累积）以提升设备路径覆盖
4. 在 CI 中固化 `ctest`（L1+L2）与 `run_all_tests.sh asan|tsan`
