import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: panel
    property var theme

    readonly property var arbRates: [125000, 250000, 500000, 1000000]
    readonly property var dataRates: [500000, 1000000, 2000000, 4000000, 5000000]
    readonly property var modeLabels: [qsTr("正常模式"), qsTr("只听模式")]
    readonly property var termLabels: [qsTr("开启"), qsTr("关闭")]
    // 0=自动（按硬件版本推断），1=60MHz（实 48），2=80MHz（实 64）
    readonly property var clockLabels: [qsTr("自动"), "60 MHz", "80 MHz"]

    color: theme.panel

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme.border
    }

    function channelSpecs() {
        var nch = canDevice.channels > 0 ? Math.min(2, canDevice.channels) : 2
        var list = []
        for (var ch = 0; ch < nch; ++ch) {
            var card = cards.itemAt(ch)
            list.push({
                "port": ch,
                "arb": panel.arbRates[card.arbIndex],
                "dat": panel.dataRates[card.dataIndex],
                "sample": card.sample,
                "listenOnly": card.listenOnly,
                "terminator": card.terminator,
                "clock": card.effectiveClock,
                "useRaw": card.useRaw,
                "arbBrp": card.rawInt(card.rawBrp),
                "arbTseg1": card.rawInt(card.rawTseg1),
                "arbTseg2": card.rawInt(card.rawTseg2),
                "arbSjw": card.rawInt(card.rawSjw),
                "arbSmp": card.rawInt(card.rawSmp),
                "dataBrp": card.rawInt(card.rawDataBrp),
                "dataTseg1": card.rawInt(card.rawDataTseg1),
                "dataTseg2": card.rawInt(card.rawDataTseg2),
                "dataSjw": card.rawInt(card.rawDataSjw),
                "dataSmp": card.rawInt(card.rawDataSmp)
            })
        }
        return list
    }

    function syncStatistics() {
        statistics.arbitrationBps = panel.arbRates[cards.itemAt(0).arbIndex]
        statistics.dataBps = panel.dataRates[cards.itemAt(0).dataIndex]
    }

    function connectDevice() {
        syncStatistics()
        deviceController.connectDevice(channelSpecs(), mergeRecv.checked)
    }

    function applyConfig() {
        syncStatistics()
        deviceController.applyConfig(channelSpecs(), mergeRecv.checked)
    }

    function disconnectDevice() {
        deviceController.disconnectDevice()
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
            // 固定预留滚动条宽度：若按 visible 动态计算会与 contentHeight 形成绑定环
            width: flick.width - 8
            spacing: 10

            SectionHeader { theme: panel.theme; title: qsTr("设备"); iconName: "plug" }

            // ---- 设备信息卡 ----
            AppCard {
                theme: panel.theme
                Layout.fillWidth: true
                implicitHeight: devCol.implicitHeight + 20

                ColumnLayout {
                    id: devCol
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AppButton {
                            theme: panel.theme
                            variant: deviceController.connected ? "ghost" : "primary"
                            text: deviceController.connected ? qsTr("断开设备") : qsTr("连接设备")
                            enabled: !deviceController.busy
                            Layout.fillWidth: true
                            onClicked: deviceController.connected ? panel.disconnectDevice()
                                                                  : panel.connectDevice()
                        }
                        StatusDot {
                            theme: panel.theme
                            dotColor: canDevice.open ? theme.rx : theme.textFaint
                            pulsing: canDevice.open
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 4

                        Text { text: qsTr("SN"); color: theme.textDim; font.pixelSize: 11 }
                        Text {
                            text: canDevice.serialNumber.length ? canDevice.serialNumber : "-"
                            color: theme.text
                            font.pixelSize: 11
                            font.family: theme.monoFamily
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text { text: qsTr("版本"); color: theme.textDim; font.pixelSize: 11; visible: canDevice.open }
                        Text {
                            text: canDevice.versionText
                            color: theme.text
                            font.pixelSize: 11
                            font.family: theme.monoFamily
                            visible: canDevice.open
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text { text: qsTr("通道"); color: theme.textDim; font.pixelSize: 11; visible: canDevice.open }
                        Text {
                            text: canDevice.channels
                            color: theme.text
                            font.pixelSize: 11
                            font.family: theme.monoFamily
                            visible: canDevice.open
                        }
                    }
                }
            }

            SectionHeader { theme: panel.theme; title: qsTr("通道配置"); iconName: "settings" }

            Repeater {
                id: cards
                model: 2
                ChannelCard {
                    theme: panel.theme
                    Layout.fillWidth: true
                    channel: index
                    running: rx.running
                }
            }

            AppCheckBox {
                id: mergeRecv
                theme: panel.theme
                text: qsTr("合并接收（两通道）")
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    Layout.fillWidth: true
                    theme: panel.theme
                    variant: "primary"
                    iconName: "refresh"
                    text: qsTr("应用配置")
                    enabled: deviceController.connected && !deviceController.busy
                    onClicked: panel.applyConfig()
                }
            }

            Text {
                id: message
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: deviceController.connected ? theme.rx : theme.textDim
                font.pixelSize: 11
                font.family: theme.sansFamily
                text: deviceController.busy ? deviceController.busyText
                      : (deviceController.connected ? qsTr("已连接 · 实时监听中") : qsTr("未连接"))
            }

            // 应用后的实际位定时（排查波特率不匹配的关键信息）
            Text {
                Layout.fillWidth: true
                visible: deviceController.configReport.length > 0
                wrapMode: Text.WordWrap
                color: theme.textFaint
                font.pixelSize: 10
                font.family: theme.monoFamily
                text: deviceController.configReport
            }
        }
    }

    // ---------------- 通道配置卡 ----------------
    component ChannelCard: Rectangle {
        id: card
        objectName: "channelCard_" + channel
        property var theme
        property int channel: 0
        property bool running: false

        property int arbIndex: 2
        property int dataIndex: 2
        property real sample: 87.5
        property bool terminator: channel === 0
        property bool listenOnly: false

        // 0=自动，1=60MHz，2=80MHz
        property int clockIndex: 0
        // 高级：原始位定时覆盖（默认给出实测可用的 250k@80MHz 参数：brp=19 tseg1=12 tseg2=1）
        property bool useRaw: false
        property string rawBrp: "19"
        property string rawTseg1: "12"
        property string rawTseg2: "1"
        property string rawSjw: "1"
        property string rawSmp: "0"
        property string rawDataBrp: "0"
        property string rawDataTseg1: "0"
        property string rawDataTseg2: "0"
        property string rawDataSjw: "0"
        property string rawDataSmp: "0"

        readonly property int effectiveClock: clockIndex === 1 ? 60000000
                                              : (clockIndex === 2 ? 80000000
                                                                  : canDevice.recommendedClock)
        readonly property var preview: canDevice.bitTimingPreview(
                                           panel.arbRates[arbIndex],
                                           panel.dataRates[dataIndex],
                                           sample, effectiveClock)

        function rawInt(s) {
            var v = parseInt(s)
            return isNaN(v) ? 0 : v
        }

        readonly property var modeLabels: [qsTr("正常模式"), qsTr("只听模式")]
        readonly property var termLabels: [qsTr("开启"), qsTr("关闭")]

        implicitHeight: cardCol.implicitHeight + 20
        color: theme.card
        border.width: 1
        border.color: running ? Qt.rgba(theme.rx.r, theme.rx.g, theme.rx.b, 0.45) : theme.border
        radius: theme.radius

        ColumnLayout {
            id: cardCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ChannelBadge { theme: card.theme; channel: card.channel; active: card.running }
                Text {
                    text: card.running ? qsTr("运行中") : qsTr("已停止")
                    color: card.running ? theme.rx : theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.sansFamily
                }
                Item { Layout.fillWidth: true }
                StatusDot {
                    theme: card.theme
                    dotColor: card.running ? theme.rx : theme.textFaint
                    pulsing: card.running
                    implicitWidth: 8
                    implicitHeight: 8
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6

                Text { text: qsTr("仲裁波特率"); color: theme.textDim; font.pixelSize: 11 }
                AppComboBox {
                    theme: card.theme
                    Layout.fillWidth: true
                    model: ["125 k", "250 k", "500 k", "1 M"]
                    currentIndex: card.arbIndex
                    onActivated: card.arbIndex = index
                }

                Text { text: qsTr("数据波特率"); color: theme.textDim; font.pixelSize: 11 }
                AppComboBox {
                    theme: card.theme
                    Layout.fillWidth: true
                    model: ["500 k", "1 M", "2 M", "4 M", "5 M"]
                    currentIndex: card.dataIndex
                    onActivated: card.dataIndex = index
                }

                Text { text: qsTr("控制器时钟"); color: theme.textDim; font.pixelSize: 11 }
                AppComboBox {
                    theme: card.theme
                    Layout.fillWidth: true
                    model: panel.clockLabels
                    currentIndex: card.clockIndex
                    onActivated: card.clockIndex = index
                }

                Text { text: qsTr("工作模式"); color: theme.textDim; font.pixelSize: 11 }
                AppComboBox {
                    theme: card.theme
                    Layout.fillWidth: true
                    model: card.modeLabels
                    onActivated: card.listenOnly = (index === 1)
                }

                Text { text: qsTr("终端电阻"); color: theme.textDim; font.pixelSize: 11 }
                AppComboBox {
                    theme: card.theme
                    Layout.fillWidth: true
                    model: card.termLabels
                    currentIndex: card.terminator ? 0 : 1
                    onActivated: card.terminator = (index === 0)
                }

                Text { text: qsTr("采样点"); color: theme.textDim; font.pixelSize: 11 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Slider {
                        id: sp
                        Layout.fillWidth: true
                        from: 50
                        to: 95
                        stepSize: 0.5
                        value: card.sample
                        onValueChanged: card.sample = value
                        background: Rectangle {
                            x: sp.leftPadding
                            y: sp.topPadding + sp.availableHeight / 2 - height / 2
                            width: sp.availableWidth
                            height: 4
                            radius: 2
                            color: theme.inputBg
                            Rectangle {
                                width: sp.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: theme.accent
                            }
                        }
                        handle: Rectangle {
                            x: sp.leftPadding + sp.visualPosition * (sp.availableWidth - width)
                            y: sp.topPadding + sp.availableHeight / 2 - height / 2
                            width: 12
                            height: 12
                            radius: 6
                            color: theme.accent
                            border.width: 2
                            border.color: theme.bg
                        }
                    }
                    Text {
                        text: card.sample.toFixed(1) + "%"
                        color: theme.text
                        font.pixelSize: 11
                        font.family: theme.monoFamily
                        Layout.preferredWidth: 38
                    }
                }
            }

            // 实际位定时（放在 GridLayout 之外，避免 columnSpan + 自动换行造成布局反复失效）
            Text {
                objectName: "bitTimingText_" + card.channel
                Layout.fillWidth: true
                color: card.preview.ok ? theme.textDim : theme.error
                font.pixelSize: 10
                font.family: theme.monoFamily
                text: {
                    var p = card.preview
                    if (!p.ok)
                        return qsTr("位定时无解（检查时钟与波特率）")
                    // 注意：QML 运行时没有 QString，空前缀用 "" 表达（历史上写 QString() 会抛
                    // ReferenceError，绑定一旦抛异常就不再重算、文本会永久停在上一个速率）。
                    // bps 来自 C++ 的 double，QML 的 arg(double) 用 %g(6) 会把 1000000 打成 1e+06。
                    return qsTr("%1 MHz · %2 bps · brp=%3 tseg1=%4 tseg2=%5 sjw=%6 · SP %7%")
                        .arg(p.clock / 1000000)
                        .arg(Number(p.arbActualBps).toFixed(0))
                        .arg(p.arbBrp)
                        .arg(p.arbTseg1)
                        .arg(p.arbTseg2)
                        .arg(p.arbSjw)
                        .arg(Number(p.arbSamplePoint).toFixed(1))
                }
            }

            AppCheckBox {
                theme: card.theme
                Layout.fillWidth: true
                text: qsTr("原始位定时（高级，覆盖公式）")
                checked: card.useRaw
                onToggled: card.useRaw = checked
            }

            RowLayout {
                Layout.fillWidth: true
                visible: card.useRaw
                spacing: 4
                Text { text: qsTr("仲裁"); color: theme.textDim; font.pixelSize: 11 }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawBrp; onTextEdited: card.rawBrp = text
                               placeholderText: "brp" }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawTseg1; onTextEdited: card.rawTseg1 = text
                               placeholderText: "tseg1" }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawTseg2; onTextEdited: card.rawTseg2 = text
                               placeholderText: "tseg2" }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawSjw; onTextEdited: card.rawSjw = text
                               placeholderText: "sjw" }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: card.useRaw
                spacing: 4
                Text { text: qsTr("数据"); color: theme.textDim; font.pixelSize: 11 }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawDataBrp; onTextEdited: card.rawDataBrp = text
                               placeholderText: "brp" }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawDataTseg1; onTextEdited: card.rawDataTseg1 = text
                               placeholderText: "tseg1" }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawDataTseg2; onTextEdited: card.rawDataTseg2 = text
                               placeholderText: "tseg2" }
                AppTextField { theme: card.theme; mono: true; Layout.fillWidth: true
                               text: card.rawDataSjw; onTextEdited: card.rawDataSjw = text
                               placeholderText: "sjw" }
            }
        }
    }
}
