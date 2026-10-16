import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: view
    property var theme
    property bool filterExpanded: false

    readonly property int wSeq: 56
    readonly property int wTime: 96
    readonly property int wCh: 50
    readonly property int wDir: 42
    readonly property int wId: 96
    readonly property int wType: 46
    readonly property int wFmt: 54
    readonly property int wDlc: 34
    readonly property int usedWidth: wSeq + wTime + wCh + wDir + wId + wType + wFmt + wDlc

    color: theme.bg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- 工具栏 ----------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: theme.panel

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                SectionHeader { theme: view.theme; title: qsTr("实时接收报文"); iconName: "link" }
                Text {
                    text: qsTr("(%1 帧 · 显示 %2)")
                          .arg(rx.totalReceived).arg(rx.displayedCount)
                    color: theme.textFaint
                    font.pixelSize: 11
                    font.family: theme.monoFamily
                }

                Item { Layout.fillWidth: true }

                ToolbarButton {
                    theme: view.theme
                    iconName: rx.paused ? "play" : "pause"
                    text: rx.paused ? qsTr("继续") : qsTr("暂停")
                    active: rx.paused
                    enabled: rx.running
                    onClicked: rx.paused = !rx.paused
                }
                ToolbarButton {
                    theme: view.theme
                    iconName: "clear"
                    text: qsTr("清空")
                    onClicked: rx.clear()
                }
                ToolbarButton {
                    theme: view.theme
                    iconName: "save"
                    text: recorder.recording ? qsTr("停止记录") : qsTr("保存日志")
                    active: recorder.recording
                    iconColor: recorder.recording ? theme.error : theme.textDim
                    onClicked: {
                        if (recorder.recording)
                            recorder.stop()
                        else
                            recorder.start("", true, true)
                    }
                }

                Rectangle { width: 1; height: 20; color: theme.border }

                AppCheckBox {
                    id: autoScroll
                    theme: view.theme
                    text: qsTr("自动滚动")
                    checked: true
                }
                AppCheckBox {
                    id: absTime
                    theme: view.theme
                    text: qsTr("绝对时间")
                    onCheckedChanged: rx.timestampMode = checked ? 1 : 0
                }

                AppTextField {
                    id: searchField
                    theme: view.theme
                    Layout.preferredWidth: 168
                    placeholderText: qsTr("搜索 ID / 数据...")
                    leftPadding: 26
                    text: rx.filter.searchText
                    onTextChanged: if (rx.filter.searchText !== text) rx.filter.searchText = text

                    Icon {
                        name: "search"
                        color: view.theme.textFaint
                        width: 13
                        height: 13
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                ToolbarButton {
                    theme: view.theme
                    iconName: "filter"
                    text: qsTr("滤波")
                    active: view.filterExpanded
                    onClicked: view.filterExpanded = !view.filterExpanded
                }
            }
        }

        // ---------------- 滤波折叠区 ----------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: filterCol.implicitHeight + 20
            visible: view.filterExpanded
            color: theme.panelAlt

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.border
            }

            ColumnLayout {
                id: filterCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text { text: qsTr("软件"); color: theme.textDim; font.pixelSize: 11 }
                    FilterCheck { theme: view.theme; text: qsTr("ID"); prop: "idEnabled" }
                    AppTextField {
                        id: idMinField
                        theme: view.theme
                        Layout.preferredWidth: 78
                        placeholderText: qsTr("起")
                        text: rx.filter.idMin.toString(16).toUpperCase()
                        onEditingFinished: rx.filter.idMin = parseInt(text, 16) || 0
                    }
                    Text { text: "~"; color: theme.textDim }
                    AppTextField {
                        id: idMaxField
                        theme: view.theme
                        Layout.preferredWidth: 78
                        placeholderText: qsTr("止")
                        text: rx.filter.idMax.toString(16).toUpperCase()
                        onEditingFinished: rx.filter.idMax = parseInt(text, 16) || 0
                    }
                    FilterCheck { theme: view.theme; text: qsTr("扩展ID"); prop: "idExtended" }
                    Text { text: qsTr("方向"); color: theme.textDim; font.pixelSize: 11 }
                    AppComboBox {
                        theme: view.theme
                        Layout.preferredWidth: 100
                        model: [qsTr("全部"), qsTr("仅接收"), qsTr("仅发送")]
                        currentIndex: rx.filter.direction
                        onActivated: rx.filter.direction = index
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: qsTr("格式"); color: theme.textDim; font.pixelSize: 11 }
                    FilterCheck { theme: view.theme; text: qsTr("标准"); prop: "showStandard" }
                    FilterCheck { theme: view.theme; text: qsTr("扩展"); prop: "showExtended" }
                    FilterCheck { theme: view.theme; text: qsTr("CAN"); prop: "showCan" }
                    FilterCheck { theme: view.theme; text: qsTr("FD"); prop: "showFd" }
                    FilterCheck { theme: view.theme; text: qsTr("远程"); prop: "showRemote" }
                    FilterCheck { theme: view.theme; text: qsTr("错误"); prop: "showError" }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: qsTr("硬件"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        id: hwStartField
                        theme: view.theme
                        Layout.preferredWidth: 78
                        placeholderText: qsTr("起")
                        text: "0"
                    }
                    Text { text: "~"; color: theme.textDim }
                    AppTextField {
                        id: hwEndField
                        theme: view.theme
                        Layout.preferredWidth: 78
                        placeholderText: qsTr("止")
                        text: "7FF"
                    }
                    AppCheckBox { id: hwExt; theme: view.theme; text: qsTr("扩展") }
                    AppButton {
                        theme: view.theme
                        variant: "subtle"
                        text: qsTr("应用")
                        implicitHeight: 26
                        onClicked: rx.filter.applyHardwareAsync(0, [{
                            "extended": hwExt.checked,
                            "start": parseInt(hwStartField.text, 16) || 0,
                            "end": parseInt(hwEndField.text, 16) || 0
                        }])
                    }
                    AppButton {
                        theme: view.theme
                        variant: "ghost"
                        text: qsTr("清除")
                        implicitHeight: 26
                        onClicked: rx.filter.clearHardwareAsync(0)
                    }
                    Text {
                        text: rx.filter.lastError
                        color: theme.error
                        font.pixelSize: 11
                        visible: rx.filter.lastError.length > 0
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        // ---------------- 表头 ----------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.rowHeight
            color: theme.panelAlt

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.border
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                spacing: 0
                HeaderCell { theme: view.theme; text: qsTr("序号 #"); width: view.wSeq }
                HeaderCell { theme: view.theme; text: qsTr("时间戳"); width: view.wTime }
                HeaderCell { theme: view.theme; text: qsTr("通道"); width: view.wCh }
                HeaderCell { theme: view.theme; text: qsTr("方向"); width: view.wDir }
                HeaderCell { theme: view.theme; text: qsTr("CAN ID"); width: view.wId }
                HeaderCell { theme: view.theme; text: qsTr("类型"); width: view.wType }
                HeaderCell { theme: view.theme; text: qsTr("格式"); width: view.wFmt }
                HeaderCell { theme: view.theme; text: qsTr("DLC"); width: view.wDlc }
                HeaderCell { theme: view.theme; text: qsTr("数据"); width: Math.max(80, list.width - view.usedWidth - 24) }
            }
        }

        // ---------------- 报文列表 ----------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: rx.frameModel
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                ScrollBar.vertical: SlimScrollBar { theme: view.theme }

                onCountChanged: {
                    // 贴底跟随：直接设置 contentY，避免 positionViewAtEnd() 在大列表下的扫描开销
                    if (autoScroll.checked && !rx.paused)
                        Qt.callLater(function() {
                            list.contentY = Math.max(0, list.contentHeight - list.height)
                        })
                }

                delegate: Rectangle {
                    id: row
                    width: list.width
                    height: theme.rowHeight
                    color: rowHover.containsMouse ? theme.panelAlt
                         : (isTx ? Qt.rgba(theme.tx.r, theme.tx.g, theme.tx.b, 0.06)
                                 : (index % 2 ? theme.zebra : "transparent"))

                    MouseArea {
                        id: rowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        spacing: 0

                        Text {
                            width: view.wSeq
                            text: sequence
                            color: theme.textFaint
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wTime
                            text: timeText
                            color: theme.textDim
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wCh
                            text: "CAN" + (channel + 1)
                            color: theme.channelColor(channel)
                            font.family: theme.monoFamily
                            font.pixelSize: 11
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wDir
                            text: isTx ? qsTr("发送") : qsTr("接收")
                            color: isTx ? theme.tx : theme.rx
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wId
                            text: idText
                            color: rowColor
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wType
                            text: typeText
                            color: theme.textDim
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wFmt
                            text: formatText
                            color: isError ? theme.error : (isFd ? theme.accent : theme.textDim)
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: view.wDlc
                            text: dlc
                            color: theme.textDim
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                        Text {
                            width: Math.max(80, list.width - view.usedWidth - 24)
                            text: dataText
                            color: isError ? theme.error : (isTx ? theme.rx : theme.text)
                            font.family: theme.monoFamily
                            font.pixelSize: theme.fontSize
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            height: parent.height
                        }
                    }
                }
            }

            // 空态占位：没有报文时不留一片纯黑
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: !rx.running ? qsTr("未开始接收\n请在左栏连接设备并应用配置")
                                  : (rx.totalReceived > 0 ? qsTr("当前筛选条件下无匹配报文")
                                                          : qsTr("等待报文…"))
                color: theme.textFaint
                font.pixelSize: 12
                font.family: theme.sansFamily
            }
        }

        // ---------------- 底部状态条 ----------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: theme.panel

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: qsTr("收 %1 fps · 发 %2 fps")
                          .arg(statistics.rxFps.toFixed(0))
                          .arg(statistics.txFps.toFixed(0))
                    color: theme.textDim
                    font.pixelSize: 10
                    font.family: theme.monoFamily
                }
                Text {
                    text: qsTr("缓冲 %1 / %2").arg(rx.bufferedCount).arg(100000)
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.monoFamily
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: rx.timestampMode === 1 ? qsTr("时间: HH:mm:ss.ms") : qsTr("时间: 相对秒")
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.monoFamily
                }
                Row {
                    spacing: 5
                    StatusDot {
                        theme: view.theme
                        implicitWidth: 7
                        implicitHeight: 7
                        dotColor: !rx.running ? theme.textFaint : (rx.paused ? theme.warn : theme.rx)
                        pulsing: rx.running && !rx.paused
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: !rx.running ? qsTr("已停止") : (rx.paused ? qsTr("已暂停") : qsTr("实时监听中"))
                        color: !rx.running ? theme.textFaint : (rx.paused ? theme.warn : theme.rx)
                        font.pixelSize: 10
                        font.family: theme.sansFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    component HeaderCell: Text {
        property var theme
        color: theme.textFaint
        font.pixelSize: 11
        font.bold: true
        verticalAlignment: Text.AlignVCenter
        height: theme.rowHeight
        elide: Text.ElideRight
    }

    component FilterCheck: AppCheckBox {
        id: fc
        property string prop
        checked: rx.filter[prop]
        onToggled: rx.filter[prop] = checked
    }
}
