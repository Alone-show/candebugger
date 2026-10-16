import QtQuick 2.15
import QtQuick.Layouts 1.15

// 轻量提示横幅：淡入 → 停留 → 淡出；悬停暂停倒计时，点击 × 立即关闭。
// 由 ToastHost 动态创建；关闭时 emit dismissed() 供宿主回收。
Rectangle {
    id: toast
    property var theme
    property string message: ""
    property string severity: "error"      // error | warn | info
    property int count: 1
    property int timeoutMs: 4000
    // 宿主写入的时间戳（ms），用于「同文案在窗口内合并」的判定
    property double lastAt: 0

    signal dismissed()

    readonly property color accentColor: severity === "warn" ? theme.warn
                                       : severity === "info" ? theme.accent
                                                             : theme.error

    width: parent ? parent.width : 0
    implicitHeight: layout.implicitHeight + 16
    height: implicitHeight
    radius: theme.radius
    // 无边框 + 半透明灰底：比之前更透（0.85 → ~0.55）。
    // 灰度按深浅主题分开取，保证「文字颜色 vs 透出的背景」对比度仍然够。
    color: theme.dark ? Qt.rgba(0.24, 0.26, 0.30, 0.55)
                      : Qt.rgba(0.80, 0.82, 0.86, 0.60)
    opacity: 0

    // 宿主在合并计数后调用，重置停留倒计时
    function kick() {
        life.restart()
    }

    function close() {
        if (!fadeOut.running)
            fadeOut.start()
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 8

        // 左侧竖条：颜色即严重度
        Rectangle {
            Layout.preferredWidth: 3
            Layout.fillHeight: true
            radius: 1.5
            color: toast.accentColor
        }

        Icon {
            Layout.alignment: Qt.AlignVCenter
            name: "warning"
            color: toast.accentColor
            implicitWidth: 15
            implicitHeight: 15
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: toast.count > 1
                  ? qsTr("%1（×%2）").arg(toast.message).arg(toast.count)
                  : toast.message
            color: toast.theme.text
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            font.family: toast.theme.sansFamily
        }

        Icon {
            id: closeIcon
            Layout.alignment: Qt.AlignVCenter
            name: "winClose"
            color: closeArea.containsMouse ? toast.theme.text : toast.theme.textDim
            implicitWidth: 13
            implicitHeight: 13

            MouseArea {
                id: closeArea
                anchors.fill: parent
                anchors.margins: -5
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toast.close()
            }
        }
    }

    // 悬停暂停倒计时（HoverHandler 不抢点击，不会挡住关闭按钮）
    HoverHandler {
        onHoveredChanged: {
            if (hovered)
                life.stop()
            else if (!fadeOut.running)
                life.restart()
        }
    }

    Timer {
        id: life
        interval: toast.timeoutMs
        running: true
        onTriggered: toast.close()
    }

    Component.onCompleted: fadeIn.start()

    NumberAnimation {
        id: fadeIn
        target: toast
        property: "opacity"
        to: 1
        duration: 160
        easing.type: Easing.OutCubic
    }

    SequentialAnimation {
        id: fadeOut
        NumberAnimation {
            target: toast
            property: "opacity"
            to: 0
            duration: 160
            easing.type: Easing.InCubic
        }
        ScriptAction { script: toast.dismissed() }
    }
}
