import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "components"

ApplicationWindow {
    id: window
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 660
    title: qsTr("USBCANFD 调试器")
    color: theme.bg

    // 自绘标题栏：不要系统标题栏与系统边框，拖动/缩放交给窗口管理器执行
    // （Qt 5.15 的 startSystemMove/startSystemResize，Wayland 与 X11 均支持吸附、平铺、跨屏）。
    // title 仅保留给任务栏 / Alt-Tab 显示。
    flags: Qt.Window | Qt.FramelessWindowHint

    Theme { id: theme }

    readonly property bool maximized: visibility === Window.Maximized
    readonly property bool resizeEnabled: visibility !== Window.Maximized
                                          && visibility !== Window.FullScreen
                                          && visibility !== Window.Minimized

    // 平台后端（main.cpp 注入 QGuiApplication::platformName()）
    readonly property string backend: typeof platformName === "undefined" ? ""
                                                                         : String(platformName)
    // 拖动/缩放策略按后端分流，原因见 updateDrag：
    // - Wayland：位置与尺寸由合成器掌控，只能靠 startSystemMove/startSystemResize
    //   （window.x/y 在 Wayland 下不可写）。
    // - X11（xcb）：Qt 的 startSystemMove/startSystemResize 只是「发一条 _NET_WM_MOVERESIZE
    //   消息」就无条件返回 true，实测 KWin 会忽略由运动事件触发的移动请求，窗口会卡住不动；
    //   因此 X11 下自己算位置/尺寸，不依赖窗口管理器。
    readonly property bool selfManagedGeometry: backend.indexOf("xcb") >= 0
                                                || backend.indexOf("x11") >= 0

    // 拖动状态（见 updateDrag）
    property point pressPos: Qt.point(0, 0)
    property bool dragStarted: false
    property bool manualDrag: false
    property point dragStartGlobal: Qt.point(0, 0)
    property point dragStartWindow: Qt.point(0, 0)

    // 缩放状态（见 beginResize）
    property int resizeEdges: 0
    property bool manualResize: false
    property point resizeStartGlobal: Qt.point(0, 0)
    property rect resizeStartGeo: Qt.rect(0, 0, 0, 0)

    function toggleMaximize() {
        if (maximized)
            showNormal()
        else
            showMaximized()
    }

    // 屏幕坐标 = 窗口位置 + 场景坐标（窗口移动时两者反相变化，故该和为不变量）
    function globalPoint(item, x, y) {
        var p = item.mapToItem(null, x, y)
        return Qt.point(p.x + window.x, p.y + window.y)
    }

    // 自己算窗口位置（X11 分支，以及平台不支持系统移动时的兜底）
    function applyManualDrag(mouse) {
        var g = globalPoint(titleDrag, mouse.x, mouse.y)
        window.x = Math.round(dragStartWindow.x + g.x - dragStartGlobal.x)
        window.y = Math.round(dragStartWindow.y + g.y - dragStartGlobal.y)
    }

    // 超过阈值（theme.dragThreshold）才接管为窗口移动，
    // 这样单击/双击不会被拖动操作吃掉。
    function updateDrag(mouse) {
        if (!dragStarted) {
            if (Math.abs(mouse.x - pressPos.x) < theme.dragThreshold
                    && Math.abs(mouse.y - pressPos.y) < theme.dragThreshold)
                return
            dragStarted = true
            manualDrag = selfManagedGeometry
            if (!manualDrag && !startSystemMove())
                manualDrag = true
            if (manualDrag) {
                // 以「按下点」为基准记录起点，窗口从按下位置起完全跟随指针，
                // 不会因为阈值丢失开头几像素的位移。
                dragStartGlobal = globalPoint(titleDrag, pressPos.x, pressPos.y)
                dragStartWindow = Qt.point(window.x, window.y)
                applyManualDrag(mouse)
            }
            return
        }
        if (manualDrag)
            applyManualDrag(mouse)
    }

    // 缩放：X11 下自己算（Qt 的 startSystemResize 在 KWin 上同样不可靠），
    // 其余平台优先交给窗口管理器，返回 false（平台不支持）时退回自算。
    function beginResize(edges, item, mouse) {
        resizeEdges = edges
        resizeStartGeo = Qt.rect(window.x, window.y, window.width, window.height)
        resizeStartGlobal = globalPoint(item, mouse.x, mouse.y)
        manualResize = selfManagedGeometry
        if (!manualResize && !startSystemResize(edges))
            manualResize = true
    }

    function applyResize(item, mouse) {
        if (!manualResize || resizeEdges === 0)
            return
        var g = globalPoint(item, mouse.x, mouse.y)
        var dx = g.x - resizeStartGlobal.x
        var dy = g.y - resizeStartGlobal.y
        var geo = resizeStartGeo
        var w = geo.width
        var h = geo.height

        if (resizeEdges & Qt.LeftEdge)
            w = geo.width - dx
        else if (resizeEdges & Qt.RightEdge)
            w = geo.width + dx
        if (resizeEdges & Qt.TopEdge)
            h = geo.height - dy
        else if (resizeEdges & Qt.BottomEdge)
            h = geo.height + dy

        // 先钳到最小尺寸，再让贴左/贴上的边跟着尺寸走（避免被钳后继续漂移）
        w = Math.max(minimumWidth, w)
        h = Math.max(minimumHeight, h)
        var x = (resizeEdges & Qt.LeftEdge) ? geo.x + (geo.width - w) : geo.x
        var y = (resizeEdges & Qt.TopEdge) ? geo.y + (geo.height - h) : geo.y

        window.width = Math.round(w)
        window.height = Math.round(h)
        window.x = Math.round(x)
        window.y = Math.round(y)
    }

    function endResize() {
        resizeEdges = 0
        manualResize = false
    }

    // ---------------- 标题栏（合并原顶部工具条）----------------
    Rectangle {
        id: titleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: theme.titlebarHeight
        color: window.active ? theme.titlebar : theme.titlebarInactive
        z: 10

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.border
        }

        // 拖动层：铺满且在所有按钮之下（后置兄弟节点压在其上），
        // 因此点击按钮不会触发窗口拖动，也不存在父节点 pointer handler 抢 grab 的问题。
        MouseArea {
            id: titleDrag
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: {
                window.pressPos = Qt.point(mouse.x, mouse.y)
                window.dragStarted = false
                window.manualDrag = false
            }
            onPositionChanged: window.updateDrag(mouse)
            onReleased: {
                window.dragStarted = false
                window.manualDrag = false
            }
            onDoubleClicked: window.toggleMaximize()
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: windowButtons.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 10

            // 品牌
            Rectangle {
                width: 24
                height: 24
                radius: 6
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#38bdf8" }
                    GradientStop { position: 1.0; color: "#6366f1" }
                }
                Text {
                    anchors.centerIn: parent
                    text: "CAN"
                    color: "white"
                    font.pixelSize: 8
                    font.bold: true
                    font.family: theme.sansFamily
                }
            }
            Text {
                text: qsTr("USBCANFD 调试器")
                color: theme.text
                font.pixelSize: 14
                font.bold: true
                font.family: theme.sansFamily
            }
            Text {
                text: "v1.0"
                color: theme.textFaint
                font.pixelSize: 10
                font.family: theme.monoFamily
            }

            Rectangle { width: 1; height: 18; color: theme.border }

            // 设备状态徽章
            Rectangle {
                Layout.preferredHeight: 26
                implicitWidth: deviceRow.implicitWidth + 16
                radius: 6
                color: theme.card
                border.width: 1
                border.color: theme.border

                RowLayout {
                    id: deviceRow
                    anchors.centerIn: parent
                    spacing: 6

                    StatusDot {
                        theme: theme
                        dotColor: canDevice.open ? theme.rx : theme.textFaint
                        pulsing: canDevice.open
                        implicitWidth: 8
                        implicitHeight: 8
                    }
                    Text {
                        text: canDevice.open ? qsTr("设备已连接") : qsTr("设备离线")
                        color: canDevice.open ? theme.text : theme.textDim
                        font.pixelSize: 11
                        font.bold: true
                        font.family: theme.sansFamily
                    }
                    Text {
                        visible: canDevice.open
                        text: "· SN " + (canDevice.serialNumber.length ? canDevice.serialNumber : "-")
                        color: theme.accent
                        font.pixelSize: 11
                        font.family: theme.monoFamily
                    }
                    Text {
                        visible: canDevice.open && canDevice.versionText.length > 0
                        text: "· " + canDevice.versionText
                        color: theme.textFaint
                        font.pixelSize: 10
                        font.family: theme.monoFamily
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ToolbarButton {
                theme: theme
                iconName: "record"
                implicitWidth: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("记录中")
                visible: recorder.recording
                active: true
                iconColor: theme.error
            }
            ToolbarButton {
                theme: theme
                iconName: "help"
                implicitWidth: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("帮助 / 关于")
            }
            ToolbarButton {
                theme: theme
                iconName: "settings"
                implicitWidth: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("设置")
            }
        }

        // 窗口控制按钮（贴右边缘，Windows 惯例）
        Rectangle {
            id: windowButtons
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: theme.titlebarButtonWidth * 3 + 1
            color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: theme.border
            }

            Row {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 0

                WindowButton {
                    theme: theme
                    objectName: "winBtnMin"
                    iconName: "winMinimize"
                    tipText: qsTr("最小化")
                    onClicked: window.showMinimized()
                }
                WindowButton {
                    theme: theme
                    objectName: "winBtnMax"
                    iconName: window.maximized ? "winRestore" : "winMaximize"
                    tipText: window.maximized ? qsTr("向下还原") : qsTr("最大化")
                    onClicked: window.toggleMaximize()
                }
                WindowButton {
                    theme: theme
                    objectName: "winBtnClose"
                    iconName: "winClose"
                    tipText: qsTr("关闭")
                    danger: true
                    onClicked: window.close()
                }
            }
        }
    }

    // ---------------- 主体 ----------------
    RowLayout {
        anchors.fill: parent
        anchors.topMargin: titleBar.height
        spacing: 0

        DevicePanel {
            theme: theme
            Layout.preferredWidth: 286
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ReceiveView {
                    theme: theme
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                StatisticsPanel {
                    theme: theme
                    Layout.preferredWidth: 312
                    Layout.fillHeight: true
                }
            }

            SendWorkspace {
                id: sendWorkspace
                theme: theme
                Layout.fillWidth: true
                Layout.preferredHeight: sendWorkspace.panelHeight
            }
        }
    }

    // ---------------- 无边框缩放热区（四边 + 四角）----------------
    // 仅最外侧 theme.resizeMargin 像素响应，最大化/全屏时整体隐藏。
    Item {
        id: resizeLayer
        anchors.fill: parent
        z: 1000
        visible: window.resizeEnabled

        component ResizeHandle: MouseArea {
            id: handle
            property int edges: 0
            cursorShape: {
                if (edges === (Qt.LeftEdge | Qt.TopEdge) || edges === (Qt.RightEdge | Qt.BottomEdge))
                    return Qt.SizeFDiagCursor
                if (edges === (Qt.RightEdge | Qt.TopEdge) || edges === (Qt.LeftEdge | Qt.BottomEdge))
                    return Qt.SizeBDiagCursor
                if (edges & (Qt.LeftEdge | Qt.RightEdge))
                    return Qt.SizeHorCursor
                return Qt.SizeVerCursor
            }
            onPressed: window.beginResize(edges, handle, mouse)
            onPositionChanged: window.applyResize(handle, mouse)
            onReleased: window.endResize()
            onCanceled: window.endResize()
        }

        ResizeHandle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: theme.resizeMargin
            edges: Qt.LeftEdge
        }
        ResizeHandle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: theme.resizeMargin
            edges: Qt.RightEdge
        }
        ResizeHandle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: theme.resizeMargin
            edges: Qt.TopEdge
        }
        ResizeHandle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: theme.resizeMargin
            edges: Qt.BottomEdge
        }
        // 四角后置，覆盖在边热区之上，取对角光标
        ResizeHandle {
            anchors.left: parent.left
            anchors.top: parent.top
            width: theme.resizeMargin * 2
            height: theme.resizeMargin * 2
            edges: Qt.LeftEdge | Qt.TopEdge
        }
        ResizeHandle {
            anchors.right: parent.right
            anchors.top: parent.top
            width: theme.resizeMargin * 2
            height: theme.resizeMargin * 2
            edges: Qt.RightEdge | Qt.TopEdge
        }
        ResizeHandle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: theme.resizeMargin * 2
            height: theme.resizeMargin * 2
            edges: Qt.LeftEdge | Qt.BottomEdge
        }
        ResizeHandle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: theme.resizeMargin * 2
            height: theme.resizeMargin * 2
            edges: Qt.RightEdge | Qt.BottomEdge
        }
    }

    // ---------------- 连接中遮罩 ----------------
    Popup {
        id: busyPopup
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: Overlay.overlay
        width: 280
        height: 132
        visible: deviceController.busy

        background: Rectangle {
            color: theme.card
            radius: theme.radius
            border.width: 1
            border.color: theme.borderLight
        }

        Column {
            anchors.centerIn: parent
            spacing: 14
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: deviceController.busy
                implicitWidth: 40
                implicitHeight: 40
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: deviceController.busyText
                color: theme.text
                font.pixelSize: 13
                font.family: theme.sansFamily
            }
        }
    }

    // ---------------- 失败弹框 ----------------
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("操作失败")
        standardButtons: Dialog.Ok
        property string message

        background: Rectangle {
            color: theme.card
            radius: theme.radius
            border.width: 1
            border.color: theme.error
        }

        Text {
            text: errorDialog.message
            color: theme.text
            wrapMode: Text.WordWrap
            width: 320
            font.family: theme.sansFamily
        }
    }

    Connections {
        target: deviceController
        function onFailed(msg) {
            errorDialog.message = msg
            errorDialog.open()
        }
    }
}
