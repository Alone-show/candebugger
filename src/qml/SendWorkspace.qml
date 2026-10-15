import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: workspace
    property var theme
    property int currentIndex: 0

    // 底部发送区高度：单帧页签只有三行表单 + 状态行，收紧后把空间让给接收列表；
    // 周期/记录回放页签要放表格，保持原高度。
    readonly property int panelHeight: currentIndex === 0 ? 215 : 256

    color: theme.panel

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
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

                Repeater {
                    model: [qsTr("单帧发送"),
                            qsTr("周期发送") + (periodicSender.count > 0 ? " (" + periodicSender.count + ")" : ""),
                            qsTr("记录 / 回放")]

                    ToolbarButton {
                        theme: workspace.theme
                        text: modelData
                        active: workspace.currentIndex === index
                        onClicked: workspace.currentIndex = index
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: workspace.currentIndex === 0
                    text: qsTr("HEX 输入 · 支持 CAN / CANFD")
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.sansFamily
                }
                Text {
                    visible: workspace.currentIndex === 1
                    text: qsTr("软件定时发送，条数不限")
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.sansFamily
                }
                Text {
                    visible: workspace.currentIndex === 2
                    text: qsTr("ASC / CSV 记录与回放")
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.family: theme.sansFamily
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: workspace.currentIndex

            SendPanel { theme: workspace.theme }
            PeriodicPanel { theme: workspace.theme }
            RecordPanel { theme: workspace.theme }
        }
    }
}
