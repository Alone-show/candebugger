import QtQuick 2.15
import QtQuick.Controls 2.15

// 深色输入框，mono 可选
TextField {
    id: control
    property var theme
    property bool mono: false

    implicitHeight: 28
    color: theme ? theme.text : "white"
    placeholderTextColor: theme ? theme.textFaint : "#64748b"
    selectionColor: theme ? theme.accent : "#38bdf8"
    selectedTextColor: theme ? theme.accentInk : "#04121f"
    font.pixelSize: 12
    font.family: (mono && theme) ? theme.monoFamily : (theme ? theme.sansFamily : "sans-serif")
    leftPadding: 8
    rightPadding: 8

    background: Rectangle {
        radius: control.theme ? control.theme.radiusSm : 5
        color: control.theme ? control.theme.inputBg : "#0b1322"
        border.width: 1
        border.color: control.activeFocus ? (control.theme ? control.theme.accent : "#38bdf8")
                                          : (control.theme ? control.theme.borderLight : "#2a3b53")
    }
}
