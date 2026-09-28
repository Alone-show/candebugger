import QtQuick 2.15

// 区块标题：可选图标 + 文本
Item {
    id: root
    property var theme
    property string title: ""
    property string iconName: ""
    property color iconColor: theme ? theme.accent : "#38bdf8"

    implicitHeight: 20
    implicitWidth: row.implicitWidth

    Row {
        id: row
        spacing: 6
        anchors.verticalCenter: parent.verticalCenter

        Icon {
            visible: root.iconName !== ""
            name: root.iconName
            color: root.iconColor
            width: 14
            height: 14
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.title
            color: root.theme ? root.theme.text : "white"
            font.bold: true
            font.pixelSize: 13
            font.family: root.theme ? root.theme.sansFamily : "sans-serif"
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
