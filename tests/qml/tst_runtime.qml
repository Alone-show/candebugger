import QtQuick 2.15
import QtTest 1.15

TestCase {
    id: testCase
    name: "Runtime"
    width: 400
    height: 300

    function test_contextPropertiesExist() {
        verify(canDevice !== undefined && canDevice !== null)
        verify(rx !== undefined && rx !== null)
        verify(statistics !== undefined && statistics !== null)
        verify(frameBuilder !== undefined && frameBuilder !== null)
        verify(periodicSender !== undefined && periodicSender !== null)
        verify(recorder !== undefined && recorder !== null)
        verify(replayer !== undefined && replayer !== null)
        verify(deviceController !== undefined && deviceController !== null)
    }

    function test_deviceControllerDefaults() {
        compare(deviceController.busy, false)
        compare(deviceController.connected, false)
        // 未连接时应用配置应为无操作且不抛错
        deviceController.applyConfig([], false)
        compare(deviceController.busy, false)
    }

    function test_rxToggles() {
        rx.paused = true
        compare(rx.paused, true)
        rx.paused = false
        compare(rx.paused, false)

        rx.timestampMode = 1
        compare(rx.timestampMode, 1)
        rx.timestampMode = 0
        compare(rx.timestampMode, 0)

        rx.clear()
        compare(rx.displayedCount, 0)
    }

    function test_filterProperties() {
        rx.filter.idEnabled = true
        rx.filter.idMin = 0x100
        rx.filter.idMax = 0x1FF
        compare(rx.filter.idEnabled, true)
        compare(rx.filter.idMin, 0x100)
        compare(rx.filter.idMax, 0x1FF)
        rx.filter.direction = 1
        compare(rx.filter.direction, 1)
        rx.filter.resetSoftware()
        compare(rx.filter.idEnabled, false)
    }

    function test_statisticsProperties() {
        statistics.topNCount = 5
        compare(statistics.topNCount, 5)
        statistics.arbitrationBps = 500000
        statistics.dataBps = 2000000
        compare(statistics.arbitrationBps, 500000)
        compare(statistics.dataBps, 2000000)
    }

    function test_frameBuilderHelpers() {
        compare(frameBuilder.dataByteCount("01 02 03", 0, 8), 3)
        compare(frameBuilder.convertData("41 42", 0, 1, 8), "AB")
        compare(frameBuilder.dataByteCount("ZZ", 0, 8), -1)
    }

    function test_recorderAndReplayerDefaults() {
        compare(recorder.recording, false)
        recorder.formatFlags = 1
        compare(recorder.formatFlags, 1)
        replayer.speed = 2.0
        compare(replayer.speed, 2.0)
        replayer.speed = 1.0
        compare(replayer.playing, false)
    }

    function test_realtimePlotTypeRegistered() {
        // StatisticsPanel 依赖 import CanDebugger 1.0 的 RealtimePlot
        var c = Qt.createComponent("qrc:/qml/StatisticsPanel.qml")
        compare(c.status, Component.Ready, c.errorString())
    }
}
