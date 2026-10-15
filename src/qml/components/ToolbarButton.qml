import QtQuick 2.15
import QtQuick.Controls 2.15

// 工具栏扁平按钮：图标 + 可选文字，含 hover/激活态
Button {
    id: control
    property var theme
    property string iconName: ""
    property bool active: false
    property color iconColor: active ? (theme ? theme.accent : "#38bdf8")
                                     : (theme ? theme.textDim : "#94a3b8")

    implicitHeight: 28
    implicitWidth: Math.max(control.text ? row.implicitWidth + 18 : 28, 28)
    font.pixelSize: 12
    font.family: theme ? theme.sansFamily : "sans-serif"

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Row {
        id: row
        spacing: 5
        anchors.centerIn: parent

        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            color: control.iconColor
            width: 14
            height: 14
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text !== ""
            text: control.text
            color: control.theme ? control.theme.text : "white"
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: control.theme ? control.theme.radiusSm : 5
        color: !control.enabled ? "transparent"
             : control.active ? (control.theme ? control.theme.border : "#1e293b")
             : control.hovered ? (control.theme ? control.theme.panelAlt : "#182338")
             : (control.theme ? control.theme.card : "#131d31")
        border.width: 1
        border.color: control.active ? (control.theme ? control.theme.borderLight : "#2a3b53")
                                     : (control.theme ? control.theme.border : "#1e293b")
        opacity: control.enabled ? 1.0 : 0.5
    }
}
