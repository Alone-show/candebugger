import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: panel
    property var theme
    property int port: 0

    property var canDlc: [0, 1, 2, 3, 4, 5, 6, 7, 8]
    property var fdDlc: [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]
    // 当前 DLC 模型（普通属性，便于切换 CAN/FD 时同步刷新，避免组合框重置索引）
    property var dlcModel: canDlc
    property int dataFormat: 0
    property int dlcValue: 8

    // 快捷周期发送：记录"本次启动"用的周期/次数（运行中改输入框不生效）
    property double quickIntervalMs: 100
    property int quickRepeat: 1

    // 状态行：多来源提示（优先级见 statusText()）
    property string quickNotice: ""
    property string busyNotice: ""
    property color busyNoticeColor: theme ? theme.textDim : "#94a3b8"
    property color quickNoticeColor: theme ? theme.textDim : "#94a3b8"

    color: theme.bg

    // 换通道后清掉上一条提示，避免把别的通道的消息显示在当前通道下
    onPortChanged: {
        panel.quickNotice = ""
        panel.busyNotice = ""
    }

    function maxBytes() { return fdCheck.checked ? 64 : 8 }
    function currentDlc() { return panel.dlcValue }

    function refreshDlc() {
        var m = fdCheck.checked ? panel.fdDlc : panel.canDlc
        panel.dlcModel = m
        var target = Math.min(panel.dlcValue, panel.maxBytes())
        var idx = 0
        for (var i = 0; i < m.length; ++i) {
            if (m[i] <= target)
                idx = i
        }
        dlcCombo.currentIndex = idx
        panel.dlcValue = m[idx]
    }

    function switchFormat(newFormat) {
        if (newFormat === panel.dataFormat)
            return
        dataField.text = frameBuilder.convertData(dataField.text, panel.dataFormat,
                                                  newFormat, panel.maxBytes())
        panel.dataFormat = newFormat
    }

    function parseId() {
        var t = idField.text.trim().replace(/^0x/i, "")
        if (t === "")
            return 0
        return parseInt(t, 16) || 0
    }

    function specMap() {
        return {
            "id": panel.parseId(),
            "extended": typeCombo.currentIndex === 1,
            "fd": fdCheck.checked,
            "remote": fmtCombo.currentIndex === 1,
            "brs": brsCheck.checked,
            "dlc": panel.currentDlc(),
            "data": dataField.text,
            "dataFormat": panel.dataFormat,
            "txm": txmCombo.currentIndex,
            "echo": echoCheck.checked,
            "crc": crcCheck.checked,
            "rolling": rollingCheck.checked,
            "increment": incrCheck.checked,
            "incrementId": idIncrCheck.checked
        }
    }

    function doSend() {
        var spec = panel.specMap()
        panel.quickNotice = ""
        if (!frameBuilder.sendFrameAsync(panel.port, spec)) {
            panel.busyNoticeColor = theme.error
            panel.busyNotice = qsTr("发送失败: ") + frameBuilder.lastError
            return
        }
        panel.busyNoticeColor = theme.textDim
        panel.busyNotice = qsTr("发送中…")
    }

    // ---------------- 快捷周期发送（独立于「周期发送」页签的配置表） ----------------

    function quickState() {
        var s = periodicSender.quickStates
        for (var i = 0; s && i < s.length; ++i) {
            if (s[i].port === panel.port)
                return s[i]
        }
        return null
    }

    function quickRunning() {
        var st = panel.quickState()
        return st ? st.active : false
    }

    function startQuick() {
        var ms = parseFloat(quickIntervalField.text)
        if (!(ms > 0))
            ms = 100
        // 次数留空/非法 -> 1 次（与输入框默认值一致）；显式 0 -> 无限循环
        var rep = parseInt(quickRepeatField.text, 10)
        if (!(rep >= 0))
            rep = 1
        panel.quickIntervalMs = ms
        panel.quickRepeat = rep
        panel.quickNotice = ""
        panel.busyNotice = ""
        // 失败原因经 quickOperationFinished 回传
        periodicSender.startQuick(panel.port, panel.specMap(), ms, rep)
    }

    function stopQuick() {
        panel.quickNotice = ""
        panel.busyNotice = ""
        periodicSender.stopQuick(panel.port)
    }

    function statusText() {
        var st = panel.quickState()
        if (st && st.active) {
            return qsTr("快捷周期发送中：通道 %1 · %2ms · %3")
                    .arg(panel.port)
                    .arg(panel.quickIntervalMs)
                    .arg(panel.quickRepeat > 0 ? qsTr("共 %1 次").arg(panel.quickRepeat)
                                               : qsTr("循环"))
        }
        if (panel.quickNotice !== "")
            return panel.quickNotice
        if (panel.busyNotice !== "")
            return panel.busyNotice
        return canDevice.open ? qsTr("就绪") : qsTr("请先连接设备")
    }

    function statusColor() {
        if (panel.quickNotice !== "")
            return panel.quickNoticeColor
        if (panel.busyNotice !== "")
            return panel.busyNoticeColor
        return theme.textDim
    }

    Connections {
        target: frameBuilder
        function onSendCompleted(ok, description, error) {
            panel.quickNotice = ""   // 最新事件拥有状态行，避免旧通知掩盖发送结果
            if (ok) {
                panel.busyNoticeColor = theme.rx
                panel.busyNotice = qsTr("已发送: ") + description
            } else {
                panel.busyNoticeColor = theme.error
                panel.busyNotice = qsTr("发送失败: ") + error
            }
        }
    }

    Connections {
        target: periodicSender
        function onQuickOperationFinished(p, ok, message) {
            if (p !== panel.port)
                return
            panel.busyNotice = ""   // 同上：快捷周期的结果优先展示
            panel.quickNoticeColor = ok ? theme.textDim : theme.error
            panel.quickNotice = message
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ---------------- 帧参数 ----------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text { text: qsTr("通道"); color: theme.textDim; font.pixelSize: 11 }
            AppComboBox {
                id: portCombo
                theme: panel.theme
                Layout.preferredWidth: 90
                model: [qsTr("通道 0"), qsTr("通道 1")]
                currentIndex: panel.port
                onCurrentIndexChanged: panel.port = currentIndex
            }

            Text { text: qsTr("帧类型"); color: theme.textDim; font.pixelSize: 11 }
            AppComboBox {
                id: typeCombo
                theme: panel.theme
                Layout.preferredWidth: 90
                model: [qsTr("标准帧"), qsTr("扩展帧")]
            }

            Text { text: qsTr("格式"); color: theme.textDim; font.pixelSize: 11 }
            AppComboBox {
                id: fmtCombo
                theme: panel.theme
                Layout.preferredWidth: 84
                model: [qsTr("数据帧"), qsTr("远程帧")]
            }

            Text { text: qsTr("CAN ID"); color: theme.textDim; font.pixelSize: 11 }
            AppTextField {
                id: idField
                theme: panel.theme
                mono: true
                Layout.preferredWidth: 100
                text: "123"
                inputMethodHints: Qt.ImhPreferNumbers
            }

            Text { text: qsTr("DLC"); color: theme.textDim; font.pixelSize: 11 }
            AppComboBox {
                id: dlcCombo
                theme: panel.theme
                Layout.preferredWidth: 68
                model: panel.dlcModel
                currentIndex: 8
                // 仅响应用户选择，避免模型切换导致 currentIndex 重置时误改 dlcValue
                onActivated: panel.dlcValue = panel.dlcModel[currentIndex]
            }

            Item { Layout.fillWidth: true }

            Text { text: qsTr("发送模式"); color: theme.textDim; font.pixelSize: 11 }
            AppComboBox {
                id: txmCombo
                theme: panel.theme
                Layout.preferredWidth: 124
                model: [qsTr("正常"), qsTr("单次"), qsTr("自发自收"), qsTr("自发自收单次")]
                currentIndex: 0
            }
        }

        // ---------------- 数据 + 发送 ----------------
        RowLayout {
            objectName: "sendRow"
            Layout.fillWidth: true
            spacing: 8

            Text { text: qsTr("数据"); color: theme.textDim; font.pixelSize: 11 }
            AppTextField {
                id: dataField
                theme: panel.theme
                mono: true
                Layout.fillWidth: true
                // 明确允许压缩：否则输入框的隐式宽度会成为本行的最小宽度（行内容 ~656px）
                Layout.minimumWidth: 120
                placeholderText: qsTr("如 01 02 03 04")
                text: "01 02 03 04 05 06 07 08"
            }
            Text {
                text: qsTr("%1 / %2 B")
                      .arg(frameBuilder.dataByteCount(dataField.text, panel.dataFormat, panel.maxBytes()))
                      .arg(panel.maxBytes())
                color: theme.textFaint
                font.pixelSize: 10
                font.family: theme.monoFamily
            }
            AppButton {
                theme: panel.theme
                variant: "primary"
                iconName: "send"
                text: qsTr("发送")
                enabled: canDevice.open
                onClicked: panel.doSend()
            }

            Rectangle { width: 1; height: 18; color: theme.border }

            Text { text: qsTr("周期"); color: theme.textDim; font.pixelSize: 11 }
            AppTextField {
                id: quickIntervalField
                objectName: "quickIntervalField"
                theme: panel.theme
                mono: true
                Layout.preferredWidth: 58
                enabled: !panel.quickRunning()
                text: "100"
                inputMethodHints: Qt.ImhPreferNumbers
            }
            Text { text: "ms"; color: theme.textFaint; font.pixelSize: 10 }

            Text { text: qsTr("次数"); color: theme.textDim; font.pixelSize: 11 }
            AppTextField {
                id: quickRepeatField
                objectName: "quickRepeatField"
                theme: panel.theme
                mono: true
                Layout.preferredWidth: 48
                enabled: !panel.quickRunning()
                text: "1"
                inputMethodHints: Qt.ImhPreferNumbers
            }
            Text { text: qsTr("0=循环"); color: theme.textFaint; font.pixelSize: 10 }

            AppButton {
                id: quickSendButton
                objectName: "quickSendButton"
                theme: panel.theme
                variant: panel.quickRunning() ? "danger" : "success"
                iconName: panel.quickRunning() ? "stop" : "play"
                text: panel.quickRunning() ? qsTr("停止") : qsTr("周期发送")
                enabled: canDevice.open
                onClicked: panel.quickRunning() ? panel.stopQuick() : panel.startQuick()

                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("把当前帧按上面的周期反复发出；次数 0 = 无限循环。\n"
                                   + "递增填充 / ID 递增 / 滚动计数 / CRC-8 会作用到每一帧。")
            }
        }

        // ---------------- 选项 ----------------
        RowLayout {
            objectName: "optionsRow"
            Layout.fillWidth: true
            spacing: 8

            // "数据格式"标签已省略：Hex/ASCII/十进制 三个单选已自明（给新开关腾宽度）
            AppRadioButton {
                id: hexRadio
                theme: panel.theme
                text: qsTr("Hex")
                checked: true
                onToggled: panel.switchFormat(0)
            }
            AppRadioButton {
                id: asciiRadio
                theme: panel.theme
                text: qsTr("ASCII")
                onToggled: panel.switchFormat(1)
            }
            AppRadioButton {
                id: decRadio
                theme: panel.theme
                text: qsTr("十进制")
                onToggled: panel.switchFormat(2)
            }

            Rectangle { width: 1; height: 18; color: theme.border }

            AppCheckBox {
                id: fdCheck
                theme: panel.theme
                text: qsTr("CANFD")
                onCheckedChanged: {
                    panel.refreshDlc()
                    dataField.text = frameBuilder.convertData(dataField.text, panel.dataFormat,
                                                              panel.dataFormat, panel.maxBytes())
                }
            }
            AppCheckBox {
                id: brsCheck
                theme: panel.theme
                text: qsTr("BRS")
                enabled: fdCheck.checked
            }
            AppCheckBox {
                id: echoCheck
                theme: panel.theme
                text: qsTr("发送回显")
                checked: true
            }

            Rectangle { width: 1; height: 18; color: theme.border }

            AppCheckBox {
                id: incrCheck
                theme: panel.theme
                text: qsTr("递增填充")
            }
            AppCheckBox {
                id: idIncrCheck
                objectName: "idIncrCheck"
                theme: panel.theme
                text: qsTr("ID 递增")
            }
            AppCheckBox {
                id: rollingCheck
                theme: panel.theme
                text: qsTr("滚动计数")
            }
            AppCheckBox {
                id: crcCheck
                theme: panel.theme
                text: qsTr("CRC-8")
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            id: status
            objectName: "sendStatus"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: panel.statusColor()
            font.pixelSize: 11
            font.family: theme.sansFamily
            text: panel.statusText()
        }
    }
}
