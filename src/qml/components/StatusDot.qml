import QtQuick 2.15

// 状态圆点，可选呼吸动画
Item {
    id: dot
    property var theme
    property color dotColor: theme ? theme.rx : "#10b981"
    property bool pulsing: false

    implicitWidth: 10
    implicitHeight: 10

    Rectangle {
        id: halo
        anchors.centerIn: parent
        width: dot.width
        height: dot.height
        radius: width / 2
        color: dot.dotColor
        opacity: 0
        visible: dot.pulsing

        SequentialAnimation on opacity {
            running: dot.pulsing
            loops: Animation.Infinite
            NumberAnimation { from: 0.55; to: 0.0; duration: 1200; easing.type: Easing.OutQuad }
            PauseAnimation { duration: 400 }
        }
        SequentialAnimation on scale {
            running: dot.pulsing
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 2.4; duration: 1200; easing.type: Easing.OutQuad }
            PauseAnimation { duration: 400 }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.round(dot.width * 0.8)
        height: width
        radius: width / 2
        color: dot.dotColor
    }
}
