cmake_minimum_required(VERSION 3.16)

project(CanDebugger VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(Threads REQUIRED)

set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# ---------------- 测试可插拔：sanitizer / 覆盖率 ----------------
# 用法：
#   cmake -B build-asan -DCANDBG_SANITIZE=address
#   cmake -B build-tsan -DCANDBG_SANITIZE=thread
#   cmake -B build-cov  -DCANDBG_COVERAGE=ON
set(CANDBG_SANITIZE "" CACHE STRING "Sanitizer 类型：address / thread / undefined / address,undefined（空=关闭）")
option(CANDBG_COVERAGE "启用 gcov 覆盖率插桩" OFF)

if(CANDBG_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

if(CANDBG_SANITIZE)
    add_compile_options(-fsanitize=${CANDBG_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${CANDBG_SANITIZE})
endif()

# ---------------- 核心库 ----------------
add_library(candbg_core STATIC
    src/core/AsyncExecutor.cpp
    src/core/AsyncExecutor.h
    src/core/BitTiming.cpp
    src/core/BitTiming.h
    src/core/CanDevice.cpp
    src/core/CanDevice.h
    src/core/DeviceController.cpp
    src/core/DeviceController.h
    src/core/CanFilter.cpp
    src/core/CanFilter.h
    src/core/FrameBuilder.cpp
    src/core/FrameBuilder.h
    src/core/FrameData.h
    src/core/PeriodicSender.cpp
    src/core/PeriodicSender.h
    src/core/Recorder.cpp
    src/core/Recorder.h
    src/core/Replayer.cpp
    src/core/Replayer.h
    src/core/RingBuffer.cpp
    src/core/RingBuffer.h
    src/core/RxWorker.cpp
    src/core/RxWorker.h
    src/core/RxManager.cpp
    src/core/RxManager.h
    src/core/Statistics.cpp
    src/core/Statistics.h
    src/core/ThemeController.cpp
    src/core/ThemeController.h
    src/core/UsbProbe.cpp
    src/core/UsbProbe.h
    src/model/FrameFilterProxyModel.cpp
    src/model/FrameFilterProxyModel.h
    src/model/FrameListModel.cpp
    src/model/FrameListModel.h
)

target_include_directories(candbg_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(candbg_core PUBLIC
    Qt5::Core
    Threads::Threads
    "${SDK_DIR}/libusbcanfd.so"
)

set_target_properties(candbg_core PROPERTIES
    BUILD_RPATH "$ORIGIN/lib"
    INSTALL_RPATH "$ORIGIN/lib"
)

# ---------------- 应用 ----------------
add_executable(CanDebugger
    src/main.cpp
    src/view/RealtimePlot.cpp
    src/view/RealtimePlot.h
    resources.qrc
)

target_link_libraries(CanDebugger PRIVATE
    candbg_core
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
)

# 版本号单一来源：CMake project(VERSION)，供 main.cpp 注入 QML（Qt.application.version）
target_compile_definitions(CanDebugger PRIVATE CANDBG_VERSION="${PROJECT_VERSION}")

set_target_properties(CanDebugger PROPERTIES
    BUILD_RPATH "$ORIGIN/lib"
    INSTALL_RPATH "$ORIGIN/lib"
)

# ---------------- 测试 ----------------
option(CANDBG_BUILD_TESTS "Build tests" ON)

if(CANDBG_BUILD_TESTS)
    find_package(Qt5 5.15 QUIET COMPONENTS Test)
    enable_testing()

    if(Qt5Test_FOUND)
        add_executable(test_bittiming tests/test_bittiming.cpp)
        target_link_libraries(test_bittiming PRIVATE candbg_core Qt5::Test)
        add_test(NAME bittiming COMMAND test_bittiming)

        add_executable(test_buffer_model tests/test_buffer_model.cpp)
        target_link_libraries(test_buffer_model PRIVATE candbg_core Qt5::Test)
        add_test(NAME buffer_model COMMAND test_buffer_model)

        add_executable(test_framebuilder tests/test_framebuilder.cpp)
        target_link_libraries(test_framebuilder PRIVATE candbg_core Qt5::Test)
        add_test(NAME framebuilder COMMAND test_framebuilder)

        add_executable(test_periodicsender tests/test_periodicsender.cpp)
        target_link_libraries(test_periodicsender PRIVATE candbg_core Qt5::Test)
        add_test(NAME periodicsender COMMAND test_periodicsender)

        add_executable(test_canfilter tests/test_canfilter.cpp)
        target_link_libraries(test_canfilter PRIVATE candbg_core Qt5::Test)
        add_test(NAME canfilter COMMAND test_canfilter)

        add_executable(test_statistics tests/test_statistics.cpp)
        target_link_libraries(test_statistics PRIVATE candbg_core Qt5::Test)
        add_test(NAME statistics COMMAND test_statistics)

        add_executable(test_plotring tests/test_plotring.cpp)
        target_link_libraries(test_plotring PRIVATE candbg_core Qt5::Test)
        add_test(NAME plotring COMMAND test_plotring)

        add_executable(test_recorder tests/test_recorder.cpp)
        target_link_libraries(test_recorder PRIVATE candbg_core Qt5::Test)
        add_test(NAME recorder COMMAND test_recorder)

        add_executable(test_replayer tests/test_replayer.cpp)
        target_link_libraries(test_replayer PRIVATE candbg_core Qt5::Test)
        add_test(NAME replayer COMMAND test_replayer)

        add_executable(test_filter_proxy tests/test_filter_proxy.cpp)
        target_link_libraries(test_filter_proxy PRIVATE candbg_core Qt5::Test)
        add_test(NAME filter_proxy COMMAND test_filter_proxy)

        add_executable(test_framelist_roles tests/test_framelist_roles.cpp)
        target_link_libraries(test_framelist_roles PRIVATE candbg_core Qt5::Test)
        add_test(NAME framelist_roles COMMAND test_framelist_roles)

        add_executable(test_ring_concurrency tests/test_ring_concurrency.cpp)
        target_link_libraries(test_ring_concurrency PRIVATE candbg_core Qt5::Test)
        add_test(NAME ring_concurrency COMMAND test_ring_concurrency)

        add_executable(test_themecontroller tests/test_themecontroller.cpp)
        target_link_libraries(test_themecontroller PRIVATE candbg_core Qt5::Test)
        add_test(NAME themecontroller COMMAND test_themecontroller)

        add_executable(test_usbprobe tests/test_usbprobe.cpp)
        target_link_libraries(test_usbprobe PRIVATE candbg_core Qt5::Test)
        add_test(NAME usbprobe COMMAND test_usbprobe)
    endif()

    # ---- QML 组件测试（QtQuickTest + offscreen） ----
    find_package(Qt5 5.15 QUIET COMPONENTS QuickTest)
    if(Qt5QuickTest_FOUND)
        add_executable(tst_qml
            tests/qml/tst_qml.cpp
            src/view/RealtimePlot.cpp
            src/view/RealtimePlot.h
            resources.qrc
        )
        target_link_libraries(tst_qml PRIVATE
            candbg_core
            Qt5::Gui
            Qt5::Qml
            Qt5::Quick
            Qt5::QuickTest
            Qt5::Test
        )
        set_target_properties(tst_qml PROPERTIES
            BUILD_RPATH "$ORIGIN/lib"
            INSTALL_RPATH "$ORIGIN/lib"
            AUTORCC ON
        )
        add_test(NAME qml
                 COMMAND tst_qml -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml")
        set_tests_properties(qml PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()

    # ---- UI 点击阻塞探针（offscreen，手动运行；不注册 ctest） ----
    add_executable(test_ui_blocking
        tests/test_ui_blocking.cpp
        src/view/RealtimePlot.cpp
        src/view/RealtimePlot.h
        resources.qrc
    )
    target_link_libraries(test_ui_blocking PRIVATE
        candbg_core
        Qt5::Gui
        Qt5::Qml
        Qt5::Quick
    )
    set_target_properties(test_ui_blocking PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
        AUTORCC ON
    )

    add_executable(device_smoke tests/device_smoke.cpp)
    target_link_libraries(device_smoke PRIVATE candbg_core)
    set_target_properties(device_smoke PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    # 设备时间戳单位标定探针，手动运行
    add_executable(device_ts_probe tests/device_ts_probe.cpp)
    target_link_libraries(device_ts_probe PRIVATE candbg_core)
    set_target_properties(device_ts_probe PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    # 波特率/位定时扫描诊断工具（需实机 + 对端），手动运行
    add_executable(device_baud_scan tests/device_baud_scan.cpp)
    target_link_libraries(device_baud_scan PRIVATE candbg_core)
    set_target_properties(device_baud_scan PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    # 模型无关的分频积暴力扫描（不知道本机时钟时定位真实位定时）
    add_executable(device_bitrate_scan tests/device_bitrate_scan.cpp)
    target_link_libraries(device_bitrate_scan PRIVATE candbg_core)
    set_target_properties(device_bitrate_scan PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    # 位定时实测诊断：打印实测可用参数（不落盘；可填到界面「原始位定时」覆盖）
    add_executable(device_calibrate tests/device_calibrate.cpp)
    target_link_libraries(device_calibrate PRIVATE candbg_core)
    set_target_properties(device_calibrate PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    # 需要真实设备，不注册进 ctest，手动运行
    add_executable(test_rx_pipeline tests/test_rx_pipeline.cpp)
    target_link_libraries(test_rx_pipeline PRIVATE candbg_core)
    set_target_properties(test_rx_pipeline PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_send_test tests/device_send_test.cpp)
    target_link_libraries(device_send_test PRIVATE candbg_core)
    set_target_properties(device_send_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    # 步骤 15 实机验证：周期发送批量提交后，同 tick 相邻帧的总线级间隔
    add_executable(device_periodic_test tests/device_periodic_test.cpp)
    target_link_libraries(device_periodic_test PRIVATE candbg_core)
    set_target_properties(device_periodic_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_filter_test tests/device_filter_test.cpp)
    target_link_libraries(device_filter_test PRIVATE candbg_core)
    set_target_properties(device_filter_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_replay_test tests/device_replay_test.cpp)
    target_link_libraries(device_replay_test PRIVATE candbg_core)
    set_target_properties(device_replay_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_merge_test tests/device_merge_test.cpp)
    target_link_libraries(device_merge_test PRIVATE candbg_core)
    set_target_properties(device_merge_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_multichannel_test tests/device_multichannel_test.cpp)
    target_link_libraries(device_multichannel_test PRIVATE candbg_core)
    set_target_properties(device_multichannel_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_stress_test tests/device_stress_test.cpp)
    target_link_libraries(device_stress_test PRIVATE candbg_core)
    set_target_properties(device_stress_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )

    add_executable(device_tx_timeout_test tests/device_tx_timeout_test.cpp)
    target_link_libraries(device_tx_timeout_test PRIVATE candbg_core)
    set_target_properties(device_tx_timeout_test PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

install(TARGETS CanDebugger RUNTIME DESTINATION bin)
