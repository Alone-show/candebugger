import QtQuick 2.15
import QtQuick.Controls 2.15

// 细滚动条
ScrollBar {
    id: control
    property var theme
    policy: ScrollBar.AsNeeded

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.pressed ? (control.theme ? control.theme.borderLight : "#2a3b53")
                               : (control.theme ? control.theme.border : "#1e293b")
        opacity: control.active ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    background: Rectangle { color: "transparent" }
}
