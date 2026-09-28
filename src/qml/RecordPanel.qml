import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: panel
    property var theme

    readonly property var speedValues: [0.5, 1, 2, 5, 10]

    color: theme.bg

    function syncFormats() {
        recorder.formatFlags = (ascCheck.checked ? 1 : 0) | (csvCheck.checked ? 2 : 0)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // ---------------- 记录 ----------------
        AppCard {
            theme: panel.theme
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                SectionHeader { theme: panel.theme; title: qsTr("记录"); iconName: "record" }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: qsTr("目录"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        id: recDir
                        theme: panel.theme
                        Layout.fillWidth: true
                        placeholderText: qsTr("留空使用当前目录")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: qsTr("格式"); color: theme.textDim; font.pixelSize: 11 }
                    AppCheckBox {
                        id: ascCheck
                        theme: panel.theme
                        text: qsTr("ASC")
                        checked: (recorder.formatFlags & 1) !== 0
                        onToggled: panel.syncFormats()
                    }
                    AppCheckBox {
                        id: csvCheck
                        theme: panel.theme
                        text: qsTr("CSV")
                        checked: (recorder.formatFlags & 2) !== 0
                        onToggled: panel.syncFormats()
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppButton {
                        theme: panel.theme
                        variant: recorder.recording ? "danger" : "success"
                        iconName: recorder.recording ? "stop" : "record"
                        text: recorder.recording ? qsTr("停止记录") : qsTr("开始记录")
                        onClicked: {
                            if (recorder.recording)
                                recorder.stop()
                            else
                                recorder.start(recDir.text, ascCheck.checked, csvCheck.checked)
                        }
                    }
                    StatusDot {
                        theme: panel.theme
                        dotColor: recorder.recording ? theme.error : theme.textFaint
                        pulsing: recorder.recording
                    }
                    Text {
                        text: recorder.recording ? qsTr("记录中") : qsTr("已停止")
                        color: recorder.recording ? theme.error : theme.textFaint
                        font.pixelSize: 11
                        font.family: theme.sansFamily
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("已写入 %1 帧 · 丢弃 %2 帧")
                          .arg(recorder.writtenCount).arg(recorder.droppedCount)
                    color: theme.textDim
                    font.pixelSize: 11
                    font.family: theme.monoFamily
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: recorder.filePath.length > 0
                    text: qsTr("文件：%1").arg(recorder.filePath)
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    visible: recorder.errorString.length > 0
                    text: recorder.errorString
                    color: theme.error
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ---------------- 回放 ----------------
        AppCard {
            theme: panel.theme
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                SectionHeader { theme: panel.theme; title: qsTr("回放"); iconName: "play" }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: qsTr("文件"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        id: playPath
                        theme: panel.theme
                        mono: true
                        Layout.fillWidth: true
                        placeholderText: qsTr("ASC / CSV 文件路径")
                        text: replayer.filePath
                    }
                    AppButton {
                        theme: panel.theme
                        variant: "subtle"
                        text: qsTr("加载")
                        implicitHeight: 28
                        enabled: !replayer.busy
                        onClicked: replayer.loadAsync(playPath.text)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: qsTr("通道"); color: theme.textDim; font.pixelSize: 11 }
                    AppComboBox {
                        theme: panel.theme
                        Layout.preferredWidth: 96
                        model: [qsTr("通道 0"), qsTr("通道 1")]
                        currentIndex: replayer.port
                        onActivated: replayer.port = index
                    }
                    Text { text: qsTr("倍速"); color: theme.textDim; font.pixelSize: 11 }
                    AppComboBox {
                        id: speedCombo
                        theme: panel.theme
                        Layout.preferredWidth: 84
                        model: ["0.5x", "1x", "2x", "5x", "10x"]
                        currentIndex: 1
                        onActivated: replayer.speed = panel.speedValues[index]
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("%1 帧").arg(replayer.frameCount)
                        color: theme.textFaint
                        font.pixelSize: 10
                        font.family: theme.monoFamily
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppButton {
                        theme: panel.theme
                        variant: "success"
                        iconName: "play"
                        text: qsTr("播放")
                        enabled: replayer.frameCount > 0 && !replayer.playing
                        onClicked: replayer.play()
                    }
                    AppButton {
                        theme: panel.theme
                        variant: "subtle"
                        iconName: replayer.paused ? "play" : "pause"
                        text: replayer.paused ? qsTr("继续") : qsTr("暂停")
                        enabled: replayer.playing
                        onClicked: replayer.paused ? replayer.resume() : replayer.pause()
                    }
                    AppButton {
                        theme: panel.theme
                        variant: "danger"
                        iconName: "stop"
                        text: qsTr("停止")
                        enabled: replayer.playing
                        onClicked: replayer.stop()
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 8
                    radius: 4
                    color: theme.inputBg

                    Rectangle {
                        width: parent.width * Math.min(100, Math.max(0, replayer.progress)) / 100
                        height: parent.height
                        radius: 4
                        color: theme.accent
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1 / %2 s")
                          .arg((replayer.positionUs / 1000000).toFixed(3))
                          .arg((replayer.durationUs / 1000000).toFixed(3))
                    color: theme.textDim
                    font.pixelSize: 11
                    font.family: theme.monoFamily
                }
                Text {
                    Layout.fillWidth: true
                    visible: replayer.errorString.length > 0
                    text: replayer.errorString
                    color: theme.error
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
