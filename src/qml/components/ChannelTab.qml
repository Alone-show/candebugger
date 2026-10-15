import QtQuick 2.15
import QtQuick.Controls 2.15

// 通道标签页：选中态用「通道色」强高亮（同色底 + 同色边框 + 底部指示条 + 加粗同色文字），
// 配色与 ChannelBadge 的通道标识一致，避免和普通工具按钮的弱激活态混淆。
Button {
    id: control
    property var theme
    property int channel: 0
    property bool selected: false
    property bool running: false
    property int entryCount: 0

    readonly property color tint: theme ? theme.channelColor(channel) : "#38bdf8"

    implicitHeight: 26
    implicitWidth: Math.max(row.implicitWidth + 22, 56)
    font.pixelSize: 12
    font.bold: control.selected
    font.family: theme ? theme.sansFamily : "sans-serif"

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Row {
        id: row
        spacing: 5
        anchors.centerIn: parent

        // 状态圆点：运行中=实心绿；未运行=空心（选中时用通道色描边）
        Rectangle {
            objectName: "dot"
            width: 7
            height: 7
            radius: 3.5
            anchors.verticalCenter: parent.verticalCenter
            color: control.running ? (control.theme ? control.theme.rx : "#10b981")
                                   : "transparent"
            border.width: control.running ? 0 : 1
            border.color: control.selected ? control.tint
                                           : (control.theme ? control.theme.borderLight : "#2a3b53")
        }

        Text {
            objectName: "label"
            text: control.text + (control.entryCount > 0 ? " (" + control.entryCount + ")" : "")
            color: control.selected ? control.tint
                                    : (control.theme ? control.theme.textDim : "#94a3b8")
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: control.theme ? control.theme.radiusSm : 5
        color: {
            if (!control.theme)
                return "transparent"
            if (control.selected)
                return Qt.rgba(control.tint.r, control.tint.g, control.tint.b, 0.20)
            return control.hovered ? control.theme.panelAlt : "transparent"
        }
        border.width: 1
        border.color: {
            if (!control.theme)
                return "transparent"
            if (control.selected)
                return Qt.rgba(control.tint.r, control.tint.g, control.tint.b, 0.80)
            return control.hovered ? control.theme.borderLight : control.theme.border
        }

        // 选中指示条（下边）
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            height: 2
            radius: 1
            visible: control.selected
            color: control.tint
        }
    }
}
