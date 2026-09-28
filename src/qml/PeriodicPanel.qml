import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: panel
    property var theme
    property int port: 0

    color: theme.bg

    ListModel { id: entries }

    function entriesArray() {
        var arr = []
        for (var i = 0; i < entries.count; ++i) {
            var r = entries.get(i)
            arr.push({
                "id": parseInt((r.idv || "0"), 16) || 0,
                "extended": r.extended,
                "fd": r.fd,
                "remote": r.remote,
                "brs": r.brs,
                "dlc": parseInt(r.dlc, 10) || 0,
                "data": r.data,
                "dataFormat": 0,
                "txm": 2,
                "echo": true,
                "enabled": r.enabled,
                "intervalMs": parseFloat(r.intervalMs) || 100,
                "repeat": parseInt(r.repeat, 10) || 0
            })
        }
        return arr
    }

    function addEntry() {
        if (entries.count >= 8)
            return
        entries.append({
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

    Connections {
        target: periodicSender
        function onOperationFinished(ok, message) {
            status.color = ok ? theme.rx : theme.error
            status.text = message
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
                text: qsTr("(硬件定时，最多 8 条；周期 ms，次数 0 = 循环)")
                color: theme.textFaint
                font.pixelSize: 10
                font.family: theme.sansFamily
            }

            Item { Layout.fillWidth: true }

            Text { text: qsTr("通道"); color: theme.textDim; font.pixelSize: 11 }
            AppComboBox {
                id: portCombo
                theme: panel.theme
                Layout.preferredWidth: 96
                model: [qsTr("通道 0"), qsTr("通道 1")]
                currentIndex: panel.port
                onCurrentIndexChanged: panel.port = currentIndex
            }

            AppButton {
                theme: panel.theme
                variant: "subtle"
                iconName: "add"
                text: qsTr("添加")
                implicitHeight: 28
                enabled: entries.count < 8
                onClicked: panel.addEntry()
            }
            AppButton {
                theme: panel.theme
                variant: "ghost"
                text: qsTr("下发配置")
                implicitHeight: 28
                enabled: canDevice.open && entries.count > 0 && !periodicSender.busy
                onClicked: panel.applyConfig()
            }
            AppButton {
                theme: panel.theme
                variant: "success"
                iconName: "play"
                text: qsTr("启动")
                implicitHeight: 28
                enabled: canDevice.open && entries.count > 0 && !periodicSender.busy
                onClicked: panel.startSending()
            }
            AppButton {
                theme: panel.theme
                variant: "danger"
                iconName: "stop"
                text: qsTr("停止")
                implicitHeight: 28
                enabled: canDevice.open && !periodicSender.busy
                onClicked: panel.stopSending()
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar { theme: panel.theme }

            delegate: Rectangle {
                id: rowItem
                width: listView.width
                height: 38
                color: index % 2 ? Qt.rgba(1, 1, 1, 0.015) : "transparent"

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
                        onToggled: entries.setProperty(index, "enabled", checked)
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
                        onToggled: entries.setProperty(index, "extended", checked)
                    }
                    AppCheckBox {
                        theme: panel.theme
                        text: qsTr("FD")
                        checked: model.fd
                        onToggled: entries.setProperty(index, "fd", checked)
                    }

                    Text { text: qsTr("ID"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 66
                        text: model.idv
                        onEditingFinished: entries.setProperty(index, "idv", text)
                    }

                    Text { text: qsTr("DLC"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 40
                        text: model.dlc
                        onEditingFinished: entries.setProperty(index, "dlc", text)
                    }

                    Text { text: qsTr("数据"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.fillWidth: true
                        Layout.minimumWidth: 140
                        text: model.data
                        onEditingFinished: entries.setProperty(index, "data", text)
                    }

                    Text { text: qsTr("周期"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 58
                        text: model.intervalMs
                        onEditingFinished: entries.setProperty(index, "intervalMs", text)
                    }
                    Text { text: "ms"; color: theme.textFaint; font.pixelSize: 10 }

                    Text { text: qsTr("次数"); color: theme.textDim; font.pixelSize: 11 }
                    AppTextField {
                        theme: panel.theme
                        mono: true
                        Layout.preferredWidth: 48
                        text: model.repeat
                        onEditingFinished: entries.setProperty(index, "repeat", text)
                    }
                    Text { text: qsTr("0=循环"); color: theme.textFaint; font.pixelSize: 10 }

                    AppButton {
                        theme: panel.theme
                        variant: "ghost"
                        iconName: "clear"
                        implicitHeight: 26
                        implicitWidth: 30
                        onClicked: entries.remove(index)
                    }
                }
            }
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
    }
}
