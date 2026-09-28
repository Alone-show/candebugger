import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

ApplicationWindow {
    id: window
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 660
    title: qsTr("USBCANFD 调试器")
    color: theme.bg

    Theme { id: theme }

    // ---------------- 顶部工具条 ----------------
    header: Rectangle {
        id: topbar
        height: 46
        color: theme.titlebar

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            // 品牌
            Rectangle {
                width: 24
                height: 24
                radius: 6
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#38bdf8" }
                    GradientStop { position: 1.0; color: "#6366f1" }
                }
                Text {
                    anchors.centerIn: parent
                    text: "CAN"
                    color: "white"
                    font.pixelSize: 8
                    font.bold: true
                    font.family: theme.sansFamily
                }
            }
            Text {
                text: qsTr("USBCANFD 调试器")
                color: theme.text
                font.pixelSize: 14
                font.bold: true
                font.family: theme.sansFamily
            }
            Text {
                text: "v1.0"
                color: theme.textFaint
                font.pixelSize: 10
                font.family: theme.monoFamily
            }

            Rectangle { width: 1; height: 18; color: theme.border }

            // 设备状态徽章
            Rectangle {
                Layout.preferredHeight: 26
                implicitWidth: deviceRow.implicitWidth + 16
                radius: 6
                color: theme.card
                border.width: 1
                border.color: theme.border

                RowLayout {
                    id: deviceRow
                    anchors.centerIn: parent
                    spacing: 6

                    StatusDot {
                        theme: theme
                        dotColor: canDevice.open ? theme.rx : theme.textFaint
                        pulsing: canDevice.open
                        implicitWidth: 8
                        implicitHeight: 8
                    }
                    Text {
                        text: canDevice.open ? qsTr("设备已连接") : qsTr("设备离线")
                        color: canDevice.open ? theme.text : theme.textDim
                        font.pixelSize: 11
                        font.bold: true
                        font.family: theme.sansFamily
                    }
                    Text {
                        visible: canDevice.open
                        text: "· SN " + (canDevice.serialNumber.length ? canDevice.serialNumber : "-")
                        color: theme.accent
                        font.pixelSize: 11
                        font.family: theme.monoFamily
                    }
                    Text {
                        visible: canDevice.open && canDevice.versionText.length > 0
                        text: "· " + canDevice.versionText
                        color: theme.textFaint
                        font.pixelSize: 10
                        font.family: theme.monoFamily
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ToolbarButton {
                theme: theme
                iconName: "record"
                implicitWidth: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("记录中")
                visible: recorder.recording
                active: true
                iconColor: theme.error
            }
            ToolbarButton {
                theme: theme
                iconName: "help"
                implicitWidth: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("帮助 / 关于")
            }
            ToolbarButton {
                theme: theme
                iconName: "settings"
                implicitWidth: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("设置")
            }
        }
    }

    // ---------------- 主体 ----------------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        DevicePanel {
            theme: theme
            Layout.preferredWidth: 286
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ReceiveView {
                    theme: theme
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                StatisticsPanel {
                    theme: theme
                    Layout.preferredWidth: 312
                    Layout.fillHeight: true
                }
            }

            SendWorkspace {
                theme: theme
                Layout.fillWidth: true
                Layout.preferredHeight: 256
            }
        }
    }

    // ---------------- 连接中遮罩 ----------------
    Popup {
        id: busyPopup
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: Overlay.overlay
        width: 280
        height: 132
        visible: deviceController.busy

        background: Rectangle {
            color: theme.card
            radius: theme.radius
            border.width: 1
            border.color: theme.borderLight
        }

        Column {
            anchors.centerIn: parent
            spacing: 14
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: deviceController.busy
                implicitWidth: 40
                implicitHeight: 40
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: deviceController.busyText
                color: theme.text
                font.pixelSize: 13
                font.family: theme.sansFamily
            }
        }
    }

    // ---------------- 失败弹框 ----------------
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("操作失败")
        standardButtons: Dialog.Ok
        property string message

        background: Rectangle {
            color: theme.card
            radius: theme.radius
            border.width: 1
            border.color: theme.error
        }

        Text {
            text: errorDialog.message
            color: theme.text
            wrapMode: Text.WordWrap
            width: 320
            font.family: theme.sansFamily
        }
    }

    Connections {
        target: deviceController
        function onFailed(msg) {
            errorDialog.message = msg
            errorDialog.open()
        }
    }
}
