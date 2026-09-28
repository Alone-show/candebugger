import QtQuick 2.15

// 指标卡片：标签 + 数值
Rectangle {
    id: card
    property var theme
    property string label: ""
    property string value: ""
    property color valueColor: theme ? theme.text : "white"
    property string iconName: ""

    implicitHeight: 58
    implicitWidth: 120

    color: theme ? theme.card : "#131d31"
    border.width: 1
    border.color: theme ? theme.border : "#1e293b"
    radius: theme ? theme.radius : 8

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 3

        Row {
            spacing: 5
            Icon {
                visible: card.iconName !== ""
                name: card.iconName
                color: card.theme ? card.theme.textFaint : "#64748b"
                width: 12
                height: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: card.label
                color: card.theme ? card.theme.textDim : "#94a3b8"
                font.pixelSize: 11
                font.family: card.theme ? card.theme.sansFamily : "sans-serif"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            text: card.value
            color: card.valueColor
            font.pixelSize: 18
            font.bold: true
            font.family: card.theme ? card.theme.monoFamily : "monospace"
            elide: Text.ElideRight
            width: parent.width
        }
    }
}
