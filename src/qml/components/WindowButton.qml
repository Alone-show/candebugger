import QtQuick 2.15
import QtQuick.Controls 2.15

// 自绘标题栏的窗口按钮（最小化 / 最大化-还原 / 关闭）
// 直角、铺满标题栏高度；hover 出底色，danger（关闭）hover 变红，符合桌面平台惯例。
// 不进 Tab 焦点链：避免抢占输入框焦点（键盘操作用 Alt+F4 等系统快捷键）。
Button {
    id: control
    property var theme
    property string iconName: ""
    property string tipText: ""
    property bool danger: false

    implicitWidth: theme ? theme.titlebarButtonWidth : 46
    implicitHeight: theme ? theme.titlebarHeight : 46
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    readonly property color closeColor: theme ? theme.titlebarClose : "#e81123"

    background: Rectangle {
        color: {
            if (!control.enabled)
                return "transparent"
            if (control.pressed)
                return control.danger ? Qt.darker(control.closeColor, 1.15)
                                      : (control.theme ? control.theme.titlebarPress : "#26344a")
            if (control.hovered)
                return control.danger ? control.closeColor
                                      : (control.theme ? control.theme.titlebarHover : "#1b2637")
            return "transparent"
        }
    }

    // 内容用 Item 包一层：Control 会把 contentItem 拉伸到可用区域，
    // 直接放 Icon 会被放大（Icon 按 width/16 缩放绘制）。
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            color: (control.danger && (control.hovered || control.pressed)) ? "#ffffff"
                  : (control.theme ? control.theme.textDim : "#94a3b8")
            width: 12
            height: 12
        }
    }

    ToolTip.visible: control.hovered && control.tipText.length > 0
    ToolTip.text: control.tipText
    ToolTip.delay: 600
}
