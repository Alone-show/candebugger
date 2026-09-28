import QtQuick 2.15

QtObject {
    id: theme

    // ---- 背景层 ----
    readonly property color bg: "#090d16"
    readonly property color surface: "#0f172a"
    readonly property color panel: "#0f172a"
    readonly property color card: "#131d31"
    readonly property color panelAlt: "#182338"
    readonly property color inputBg: "#0b1322"
    readonly property color titlebar: "#080b12"

    // ---- 边框 ----
    readonly property color border: "#1e293b"
    readonly property color borderLight: "#2a3b53"

    // ---- 文本 ----
    readonly property color text: "#e2e8f0"
    readonly property color textDim: "#94a3b8"
    readonly property color textFaint: "#64748b"

    // ---- 语义色（统一约定：接收=emerald，发送=sky，错误=danger）----
    readonly property color accent: "#38bdf8"
    readonly property color accentDim: "#0ea5e9"
    readonly property color rx: "#10b981"
    readonly property color tx: "#38bdf8"
    readonly property color warn: "#f59e0b"
    readonly property color error: "#ef4444"
    readonly property color accentInk: "#04121f"

    // ---- 通道标识色 ----
    readonly property color ch1: "#10b981"
    readonly property color ch2: "#f59e0b"

    // ---- 字体（未安装时回退通用族）----
    readonly property string monoFamily: "monospace"
    readonly property string sansFamily: "sans-serif"

    // ---- 尺寸 ----
    readonly property int rowHeight: 26
    readonly property int fontSize: 12
    readonly property int radius: 8
    readonly property int radiusSm: 5
    readonly property int spacing: 8

    function channelColor(index) {
        return index === 0 ? ch1 : (index === 1 ? ch2 : textDim)
    }
}
