import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: panel
    property var theme
    property int port: 0

    color: theme.bg

    // 每通道一张独立的周期表（软件定时，条数不限）
    ListModel { id: entries0 }
    ListModel { id: entries1 }

    property string noticeText: ""
    property color noticeColor: theme ? theme.textDim : "#94a3b8"

    function modelFor(p) {
        return p === 1 ? entries1 : entries0
    }

    function channelState(p) {
        var s = periodicSender.channelStates
        return (s && p >= 0 && p < s.length) ? s[p] : null
    }
    function channelCount(p) {
        var c = channelState(p)
        return c ? c.count : 0
    }
    function channelActive(p) {
        var c = channelState(p)
        return c ? c.active : false
    }
    function channelError(p) {
        var c = channelState(p)
        return (c && c.error) ? c.error : ""
    }

    function entriesArray() {
        var m = modelFor(panel.port)
        var arr = []
        for (var i = 0; i < m.count; ++i) {
            var r = m.get(i)
            arr.push({
                "id": parseInt((r.idv || "0"), 16) || 0,
                "extended": r.extended,
                "fd": r.fd,
                "remote": r.remote,
                "brs": r.brs,
                "dlc": parseInt(r.dlc, 10) || 0,
                "data": r.data,
                "dataFormat": 0,
                "txm": 0,        // 正常模式：上总线并等待 ACK（自发自收需显式选择）
                "echo": true,
                "enabled": r.enabled,
                "intervalMs": parseFloat(r.intervalMs) || 100,
                "repeat": parseInt(r.repeat, 10) || 0
            })
        }
        return arr
    }

    function addEntry() {
        modelFor(panel.port).append({
            "enabled": true,
            "intervalMs": "100",
            "repeat": "0",
            "idv": "300",
            "extended": false,
            "fd": false,
            "remote": false,
            "brs": false,
            "dlc": "8",
            "data": "01 02 03 04 05 06 07 08"
        })
    }

    function applyConfig() {
        periodicSender.applyAsync(panel.port, panel.entriesArray())
    }

    function startSending() {
        if (!periodicSender.applyAsync(panel.port, panel.entriesArray()))
            return
        periodicSender.startAsync(panel.port)
    }

    function stopSending() {
        periodicSender.stopAsync(panel.port)
    }

    // 切换通道时清掉上一条通知
    onPortChanged: {
        panel.noticeText = ""
        panel.noticeColor = panel.theme ? panel.theme.textDim : "#94a3b8"
    }

    Connections {
        target: periodicSender
        function onOperationFinished(port, ok, message) {
            if (port !== panel.port)
                return
            panel.noticeColor = ok ? panel.theme.rx : panel.theme.error
            panel.noticeText = message
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            SectionHeader { theme: panel.theme; title: qsTr("周期发送"); iconName: "loop" }
            Text {
                text: qsTr("(软件定时，条数不限；周期 ms，次数 0 = 循环)")
                color: theme.textFaint
                font.pixelSize: 10
                font.family: theme.sansFamily
            }

            Item { Layout.fillWidth: true }

            // ---- 通道标签页 ----
            RowLayout {
                spacing: 4

                Repeater {
                    model: 2
                    delegate: ChannelTab {
                        theme: panel.theme
                        channel: index
                        selected: panel.port === index
                        running: panel.channelActive(index)
                        entryCount: panel.channelCount(index)
                        text: qsTr("通道 %1").arg(index)
                        onClicked: panel.port = index
                    }
                }
            }

            AppButton {
                theme: panel.theme
                variant: "subtle"
                iconName: "add"
                text: qsTr("添加")
                implicitHeight: 28
                onClicked: panel.addEntry()
            }
            AppButton {
                theme: panel.theme
                variant: "ghost"
                text: qsTr("下发配置")
                implicitHeight: 28
                enabled: canDevice.open && panel.modelFor(panel.port).count > 0
                onClicked: panel.applyConfig()
            }
            AppButton {
                theme: panel.theme
                variant: "success"
                iconName: "play"
                text: qsTr("启动")
                implicitHeight: 28
                enabled: canDevice.open && panel.modelFor(panel.port).count > 0
                         && !panel.channelActive(panel.port)
                onClicked: panel.startSending()
            }
            AppButton {
                theme: panel.theme
                variant: "danger"
                iconName: "stop"
                text: qsTr("停止")
                implicitHeight: 28
                enabled: canDevice.open && panel.channelActive(panel.port)
                onClicked: panel.stopSending()
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.modelFor(panel.port)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar { theme: panel.theme }

            delegate: Rectangle {
                id: rowItem
                width: listView.width
                height: 38
                color: index % 2 ? theme.zebra : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 8

                    AppCheckBox {
                        theme: panel.theme
                        text: ""
                        implicitWidth: 24
                        checked: model.enabled
                        onToggled: panel.modelFor(panel.port).setProperty(index, "enabled", checked)
                    }
                    Text {
                        text: "#" + (index + 1)
                        color: theme.textFaint
                        font.pixelSize: 11
                        font.family: theme.monoFamily
                        Layout.preferredWidth: 24
                    }
                    AppCheckBox {
                        theme: panel.theme
                        text: qsTr("扩展")
                        checked: model.extended
                        onToggled: panel.modelFor(panel.port).setProperty(index, "extended", checked)
                    }
                    AppCheckBox {
                        theme: panel.theme
                        text: qsTr("FD")
                        checked: model.fd
                        onToggled: panel.modelFor(panel.port).setProperty(index, "fd", checked)
                    }

                    Text { text: qsTr("ID"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 66
                        text: model.idv
                        onEditingFinished: panel.modelFor(panel.port).setProperty(index, "idv", text)
                    }

                    Text { text: qsTr("DLC"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 40
                        text: model.dlc
                        onEditingFinished: panel.modelFor(panel.port).setProperty(index, "dlc", text)
                    }

                    Text { text: qsTr("数据"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.fillWidth: true
                        Layout.minimumWidth: 140
                        text: model.data
                        onEditingFinished: panel.modelFor(panel.port).setProperty(index, "data", text)
                    }

                    Text { text: qsTr("周期"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 58
                        text: model.intervalMs
                        onEditingFinished: panel.modelFor(panel.port).setProperty(index, "intervalMs", text)
                    }
                    Text { text: "ms"; color: theme.textFaint; font.pixelSize: 10 }

                    Text { text: qsTr("次数"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 48
                        text: model.repeat
                        onEditingFinished: panel.modelFor(panel.port).setProperty(index, "repeat", text)
                    }
                    Text { text: qsTr("0=循环"); color: theme.textFaint; font.pixelSize: 10 }

                    AppButton {
                        theme: panel.theme
                        variant: "ghost"
                        iconName: "clear"
                        implicitHeight: 26
                        implicitWidth: 30
                        onClicked: panel.modelFor(panel.port).remove(index)
                    }
                }
            }
        }

        Text {
            id: status
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: panel.noticeText !== "" ? panel.noticeColor : theme.textDim
            font.pixelSize: 11
            font.family: theme.sansFamily
            text: {
                if (!canDevice.open)
                    return qsTr("请先连接设备")
                if (panel.noticeText !== "")
                    return panel.noticeText
                var e = panel.channelError(panel.port)
                if (e !== "")
                    return e
                if (panel.channelActive(panel.port))
                    return qsTr("通道 %1 周期发送中…").arg(panel.port)
                return qsTr("就绪")
            }
        }
    }
}
