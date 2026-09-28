import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: panel
    property var theme
    property int port: 0
    property bool advanced: false

    property var canDlc: [0, 1, 2, 3, 4, 5, 6, 7, 8]
    property var fdDlc: [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]
    // 当前 DLC 模型（普通属性，便于切换 CAN/FD 时同步刷新，避免组合框重置索引）
    property var dlcModel: canDlc
    property int dataFormat: 0
    property int dlcValue: 8

    color: theme.bg

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
            "increment": incrCheck.checked
        }
    }

    function doSend() {
        var spec = panel.specMap()
        if (!frameBuilder.sendFrameAsync(panel.port, spec)) {
            status.color = theme.error
            status.text = qsTr("发送失败: ") + frameBuilder.lastError
            return
        }
        status.color = theme.textDim
        status.text = qsTr("发送中…")
    }

    function addToList() {
        var spec = panel.specMap()
        listModel.append({
            "idv": spec.id,
            "extended": spec.extended,
            "fd": spec.fd,
            "remote": spec.remote,
            "brs": spec.brs,
            "dlc": spec.dlc,
            "data": spec.data,
            "dataFormat": spec.dataFormat,
            "txm": spec.txm,
            "echo": spec.echo,
            "crc": spec.crc,
            "rolling": spec.rolling,
            "increment": spec.increment,
            "desc": frameBuilder.describe(spec)
        })
    }

    function batchList(list) {
        var arr = []
        for (var i = 0; i < list.length; ++i) {
            var r = list[i]
            arr.push({
                "id": r.idv, "extended": r.extended, "fd": r.fd, "remote": r.remote,
                "brs": r.brs, "dlc": r.dlc, "data": r.data, "dataFormat": r.dataFormat,
                "txm": r.txm, "echo": r.echo, "crc": r.crc, "rolling": r.rolling,
                "increment": r.increment
            })
        }
        return arr
    }

    function sendAll() {
        var arr = panel.batchList(listModel)
        var n = frameBuilder.sendBatchAsync(panel.port, arr)
        status.color = theme.textDim
        status.text = qsTr("已加入发送队列 %1/%2 帧").arg(n).arg(arr.length)
    }

    function sendSelected() {
        if (listView.currentIndex < 0) {
            status.color = theme.error
            status.text = qsTr("请先选中列表中的一帧")
            return
        }
        var r = listModel.get(listView.currentIndex)
        var arr = panel.batchList([r])
        var n = frameBuilder.sendBatchAsync(panel.port, arr)
        status.color = theme.textDim
        status.text = qsTr("已加入发送队列 %1/1 帧").arg(n)
    }

    Connections {
        target: frameBuilder
        function onSendCompleted(ok, description, error) {
            if (ok) {
                status.color = theme.rx
                status.text = qsTr("已发送: ") + description
            } else {
                status.color = theme.error
                status.text = qsTr("发送失败: ") + error
            }
        }
    }

    ListModel { id: listModel }

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
            Layout.fillWidth: true
            spacing: 8

            Text { text: qsTr("数据"); color: theme.textDim; font.pixelSize: 11 }
            AppTextField {
                id: dataField
                theme: panel.theme
                mono: true
                Layout.fillWidth: true
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
            AppButton {
                theme: panel.theme
                variant: "ghost"
                iconName: "add"
                text: qsTr("加入列表")
                onClicked: panel.addToList()
            }
            AppButton {
                theme: panel.theme
                variant: panel.advanced ? "subtle" : "ghost"
                iconName: panel.advanced ? "chevronDown" : "chevronUp"
                text: qsTr("高级")
                onClicked: panel.advanced = !panel.advanced
            }
        }

        // ---------------- 选项 ----------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text { text: qsTr("数据格式"); color: theme.textDim; font.pixelSize: 11 }
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
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: theme.textDim
            font.pixelSize: 11
            font.family: theme.sansFamily
            text: canDevice.open ? qsTr("就绪") : qsTr("请先连接设备")
        }

        // ---------------- 高级：多帧列表 ----------------
        AppCard {
            theme: panel.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.advanced
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("多帧列表")
                        color: theme.text
                        font.pixelSize: 12
                        font.bold: true
                        font.family: theme.sansFamily
                    }
                    Text {
                        text: qsTr("%1 帧").arg(listModel.count)
                        color: theme.textFaint
                        font.pixelSize: 10
                        font.family: theme.monoFamily
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        theme: panel.theme
                        variant: "success"
                        text: qsTr("发送全部")
                        implicitHeight: 26
                        enabled: canDevice.open && listModel.count > 0
                        onClicked: panel.sendAll()
                    }
                    AppButton {
                        theme: panel.theme
                        variant: "subtle"
                        text: qsTr("发送选中")
                        implicitHeight: 26
                        enabled: canDevice.open && listModel.count > 0
                        onClicked: panel.sendSelected()
                    }
                    AppButton {
                        theme: panel.theme
                        variant: "ghost"
                        text: qsTr("清空")
                        implicitHeight: 26
                        onClicked: { listModel.clear(); listView.currentIndex = -1 }
                    }
                }

                ListView {
                    id: listView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: listModel
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: SlimScrollBar { theme: panel.theme }

                    delegate: Rectangle {
                        width: listView.width
                        height: 24
                        color: (index === listView.currentIndex) ? theme.panelAlt
                               : (index % 2 ? Qt.rgba(1, 1, 1, 0.015) : "transparent")

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            text: (index + 1) + ".  " + desc
                            color: theme.text
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: listView.currentIndex = index
                        }
                    }
                }
            }
        }
    }
}
