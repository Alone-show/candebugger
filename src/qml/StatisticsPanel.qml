import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import CanDebugger 1.0
import "components"

Rectangle {
    id: panel
    property var theme

    color: theme.panel

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme.border
    }

    function clearPlots() {
        fpsPlot.clear()
        loadPlot.clear()
    }

    // 每 1s 聚合一次，向趋势图追加当前值（不新增逐帧信号）
    Connections {
        target: statistics
        function onUpdated() {
            fpsPlot.append(0, statistics.rxFps)
            fpsPlot.append(1, statistics.txFps)
            loadPlot.append(0, statistics.busLoad)
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        contentWidth: width
        contentHeight: col.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar { theme: panel.theme }

        ColumnLayout {
            id: col
            width: flick.width - (flick.ScrollBar.vertical.visible ? 8 : 0)
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SectionHeader { theme: panel.theme; title: qsTr("统计与总线负载"); iconName: "chart" }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("聚合")
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.sansFamily
                }
                AppButton {
                    theme: panel.theme
                    variant: "ghost"
                    iconName: "refresh"
                    text: qsTr("复位")
                    implicitHeight: 26
                    onClicked: {
                        statistics.reset()
                        panel.clearPlots()
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8

                MetricCard {
                    theme: panel.theme
                    Layout.fillWidth: true
                    label: qsTr("接收帧 (RX)")
                    value: statistics.rxCount.toString()
                    valueColor: theme.rx
                }
                MetricCard {
                    theme: panel.theme
                    Layout.fillWidth: true
                    label: qsTr("发送帧 (TX)")
                    value: statistics.txCount.toString()
                    valueColor: theme.tx
                }
                MetricCard {
                    theme: panel.theme
                    Layout.fillWidth: true
                    label: qsTr("错误帧 (ERR)")
                    value: statistics.errorFrames.toString()
                    valueColor: theme.error
                }
                MetricCard {
                    theme: panel.theme
                    Layout.fillWidth: true
                    label: qsTr("总线状态")
                    value: !rx.running ? qsTr("STOP") : (rx.paused ? qsTr("PAUSED") : qsTr("ACTIVE"))
                    valueColor: !rx.running ? theme.textFaint : (rx.paused ? theme.warn : theme.rx)
                }
            }

            // ---- 总线负载 ----
            AppCard {
                theme: panel.theme
                Layout.fillWidth: true
                implicitHeight: 84

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("当前总线负载")
                            color: theme.text
                            font.pixelSize: 12
                            font.bold: true
                            font.family: theme.sansFamily
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: statistics.busLoad.toFixed(1) + "%"
                            color: statistics.busLoad > 80 ? theme.warn : theme.rx
                            font.pixelSize: 15
                            font.bold: true
                            font.family: theme.monoFamily
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 8
                        radius: 4
                        color: theme.inputBg

                        Rectangle {
                            width: parent.width * Math.min(100, Math.max(0, statistics.busLoad)) / 100
                            height: parent.height
                            radius: 4
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: theme.rx }
                                GradientStop { position: 1.0; color: theme.accent }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "0%"; color: theme.textFaint; font.pixelSize: 9; font.family: theme.monoFamily }
                        Item { Layout.fillWidth: true }
                        Text { text: "100%"; color: theme.textFaint; font.pixelSize: 9; font.family: theme.monoFamily }
                    }
                }
            }

            // ---- 负载趋势 ----
            AppCard {
                theme: panel.theme
                Layout.fillWidth: true
                implicitHeight: 132

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("总线负载趋势 (%)")
                            color: theme.text
                            font.pixelSize: 12
                            font.bold: true
                            font.family: theme.sansFamily
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("最近 %1 点").arg(loadPlot.maxPoints)
                            color: theme.textFaint
                            font.pixelSize: 10
                            font.family: theme.monoFamily
                        }
                    }

                    RealtimePlot {
                        id: loadPlot
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: 1
                        maxPoints: 150
                        autoScale: false
                        minValue: 0
                        maxValue: 100
                        fill: true
                        lineWidth: 1.6
                        lineColors: [theme.rx]
                    }
                }
            }

            // ---- 帧率趋势 ----
            AppCard {
                theme: panel.theme
                Layout.fillWidth: true
                implicitHeight: 118

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("帧率趋势 (fps)")
                            color: theme.text
                            font.pixelSize: 12
                            font.bold: true
                            font.family: theme.sansFamily
                        }
                        Item { Layout.fillWidth: true }
                        Row {
                            spacing: 5
                            Rectangle { width: 8; height: 8; radius: 2; color: theme.rx; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: qsTr("收"); color: theme.textFaint; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                            Rectangle { width: 8; height: 8; radius: 2; color: theme.tx; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: qsTr("发"); color: theme.textFaint; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }

                    RealtimePlot {
                        id: fpsPlot
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: 2
                        maxPoints: 150
                        autoScale: true
                        lineWidth: 1.5
                        lineColors: [theme.rx, theme.tx]
                    }
                }
            }

            // ---- 错误计数 ----
            AppCard {
                theme: panel.theme
                Layout.fillWidth: true
                implicitHeight: 78

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("错误计数")
                            color: theme.text
                            font.pixelSize: 12
                            font.bold: true
                            font.family: theme.sansFamily
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            theme: panel.theme
                            variant: "ghost"
                            text: qsTr("清零")
                            implicitHeight: 24
                            onClicked: statistics.reset()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: qsTr("错误帧"); color: theme.textDim; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: statistics.errorFrames.toString()
                            color: theme.error
                            font.pixelSize: 13
                            font.bold: true
                            font.family: theme.monoFamily
                        }
                    }
                }
            }

            // ---- Top-N ----
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("报文 Top-N")
                    color: theme.text
                    font.pixelSize: 12
                    font.bold: true
                    font.family: theme.sansFamily
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("前 %1 条 · 总帧 %2").arg(statistics.topNCount).arg(statistics.totalFrames)
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.monoFamily
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 220
                color: theme.card
                border.width: 1
                border.color: theme.border
                radius: theme.radius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.rowHeight
                        color: "transparent"

                        Row {
                            anchors.fill: parent
                            Text { width: 120; text: qsTr("ID"); color: theme.textFaint; font.pixelSize: 11; font.bold: true; verticalAlignment: Text.AlignVCenter; height: parent.height }
                            Text { width: 60; text: qsTr("类型"); color: theme.textFaint; font.pixelSize: 11; font.bold: true; verticalAlignment: Text.AlignVCenter; height: parent.height }
                            Text { text: qsTr("计数"); color: theme.textFaint; font.pixelSize: 11; font.bold: true; verticalAlignment: Text.AlignVCenter; height: parent.height }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                    ListView {
                        id: topList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: statistics.topN
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: SlimScrollBar { theme: panel.theme }

                        delegate: Rectangle {
                            width: topList.width
                            height: theme.rowHeight
                            color: index % 2 ? Qt.rgba(1, 1, 1, 0.015) : "transparent"

                            Row {
                                anchors.fill: parent
                                Text {
                                    width: 120
                                    text: modelData.idText
                                    color: theme.text
                                    font.family: theme.monoFamily
                                    font.pixelSize: theme.fontSize
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                    height: parent.height
                                }
                                Text {
                                    width: 60
                                    text: modelData.extended ? qsTr("扩展") : qsTr("标准")
                                    color: theme.textDim
                                    font.family: theme.monoFamily
                                    font.pixelSize: theme.fontSize
                                    verticalAlignment: Text.AlignVCenter
                                    height: parent.height
                                }
                                Text {
                                    text: modelData.count
                                    color: theme.accent
                                    font.family: theme.monoFamily
                                    font.pixelSize: theme.fontSize
                                    verticalAlignment: Text.AlignVCenter
                                    height: parent.height
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
