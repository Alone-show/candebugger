import QtQuick 2.15
import QtQuick.Controls 2.15

// 深色下拉框
ComboBox {
    id: control
    property var theme

    implicitHeight: 28
    font.pixelSize: 12
    font.family: theme ? theme.sansFamily : "sans-serif"

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    delegate: ItemDelegate {
        id: item
        width: control.width
        height: 26
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: modelData !== undefined ? modelData : ""
            color: control.theme ? control.theme.text : "white"
            font: control.font
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: item.highlighted ? (control.theme ? control.theme.panelAlt : "#182338") : "transparent"
            radius:  4
        }
    }

    indicator: Icon {
        x: control.width - width - 6
        y: control.topPadding + (control.availableHeight - height) / 2
        name: "chevronDown"
        color: control.theme ? control.theme.textDim : "#94a3b8"
        width: 12
        height: 12
    }

    contentItem: Text {
        leftPadding: 8
        rightPadding: control.indicator.width + 6
        text: control.displayText
        color: control.theme ? control.theme.text : "white"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font: control.font
    }

    background: Rectangle {
        radius: control.theme ? control.theme.radiusSm : 5
        color: control.theme ? control.theme.inputBg : "#0b1322"
        border.width: 1
        border.color: control.activeFocus ? (control.theme ? control.theme.accent : "#38bdf8")
                                          : (control.theme ? control.theme.borderLight : "#2a3b53")
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4

        background: Rectangle {
            color: control.theme ? control.theme.card : "#131d31"
            border.color: control.theme ? control.theme.borderLight : "#2a3b53"
            border.width: 1
            radius: 6
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
