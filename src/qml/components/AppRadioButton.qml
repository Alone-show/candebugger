import QtQuick 2.15
import QtQuick.Controls 2.15

// 深色单选框
RadioButton {
    id: control
    property var theme

    spacing: 6
    implicitHeight: 24
    font.pixelSize: 12
    font.family: theme ? theme.sansFamily : "sans-serif"

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: width / 2
        color: control.theme ? control.theme.inputBg : "#0b1322"
        border.width: 1
        border.color: control.checked ? (control.theme ? control.theme.accent : "#38bdf8")
                                      : (control.theme ? control.theme.borderLight : "#2a3b53")

        Rectangle {
            visible: control.checked
            width: 8
            height: 8
            radius: 4
            color: control.theme ? control.theme.accent : "#38bdf8"
            anchors.centerIn: parent
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.theme ? control.theme.text : "white")
                               : (control.theme ? control.theme.textFaint : "#64748b")
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
