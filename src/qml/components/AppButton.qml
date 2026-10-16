import QtQuick 2.15
import QtQuick.Controls 2.15

// 主操作按钮：primary / success / danger / ghost / subtle
Button {
    id: control
    property var theme
    property string variant: "ghost"
    property string iconName: ""

    implicitHeight: 30
    implicitWidth: Math.max(row.implicitWidth + 24, 64)
    font.pixelSize: 12
    font.bold: variant === "primary" || variant === "success" || variant === "danger"
    font.family: theme ? theme.sansFamily : "sans-serif"

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    function baseColor() {
        if (!theme)
            return "transparent"
        if (variant === "primary") return theme.accent
        if (variant === "success") return theme.rx
        if (variant === "danger") return theme.error
        if (variant === "subtle") return theme.panelAlt
        return "transparent"
    }

    function fgColor() {
        if (!theme)
            return "white"
        return (variant === "primary" || variant === "success" || variant === "danger")
                ? theme.accentInk : theme.text
    }

    contentItem: Row {
        id: row
        spacing: 6
        anchors.centerIn: parent

        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            color: control.fgColor()
            width: 14
            height: 14
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text !== ""
            text: control.text
            color: control.fgColor()
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: control.theme ? control.theme.radiusSm : 5
        color: {
            if (!control.theme)
                return "transparent"
            if (control.variant === "ghost") {
                if (control.enabled && (control.pressed || control.hovered))
                    return control.theme.panelAlt
                return "transparent"
            }
            var base = control.baseColor()
            if (!control.enabled)
                return base
            // hover/pressed 的明暗方向随主题翻转（浅色下压暗、深色下提亮）
            if (control.pressed)
                return control.theme.pressTint(base)
            return control.hovered ? control.theme.hoverTint(base) : base
        }
        border.width: (control.variant === "ghost" || control.activeFocus) ? 1 : 0
        border.color: control.activeFocus ? (control.theme ? control.theme.accent : "#38bdf8")
                                          : (control.theme ? control.theme.borderLight : "#2a3b53")
        opacity: control.enabled ? 1.0 : 0.4
    }
}
