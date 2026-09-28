import QtQuick 2.15

// 通道徽章：CAN1 / CAN2，按通道着色
Rectangle {
    id: badge
    property var theme
    property int channel: 0
    property bool active: false
    property bool showDot: false

    readonly property color tint: theme ? theme.channelColor(channel) : "#38bdf8"

    implicitWidth: txt.implicitWidth + (showDot ? dot.width + 5 : 0) + 12
    implicitHeight: 18
    radius: 3
    color: Qt.rgba(tint.r, tint.g, tint.b, active ? 0.18 : 0.10)
    border.width: 1
    border.color: Qt.rgba(tint.r, tint.g, tint.b, active ? 0.5 : 0.28)

    Rectangle {
        id: dot
        visible: badge.showDot
        width: 6
        height: 6
        radius: 3
        color: badge.tint
        anchors.left: parent.left
        anchors.leftMargin: 5
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        id: txt
        text: "CAN" + (badge.channel + 1)
        color: badge.tint
        font.pixelSize: 10
        font.bold: true
        font.family: badge.theme ? badge.theme.monoFamily : "monospace"
        anchors.left: badge.showDot ? dot.right : parent.left
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
    }
}
