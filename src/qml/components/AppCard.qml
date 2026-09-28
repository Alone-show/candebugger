import QtQuick 2.15

// 卡片容器：深色背景 + 细边框 + 圆角，默认内容置于内边距区。
Rectangle {
    id: card
    property var theme
    property int padding: 10
    default property alias content: inner.data

    color: theme ? theme.card : "#131d31"
    border.width: 1
    border.color: theme ? theme.border : "#1e293b"
    radius: theme ? theme.radius : 8

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: card.padding
    }
}
