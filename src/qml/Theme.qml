import QtQuick 2.15

// 设计系统：语义令牌 + 深/浅两套色板。
//
// 令牌名与用法保持不变（`theme.bg` / `theme.textDim` / `theme.channelColor(i)` …），
// 只把「色值」抽成 darkTokens / lightTokens 两张表，由 dark 开关二选一。
// 所有 `theme.xxx` 的绑定都依赖 `tokens` → `dark`，因此切换主题时界面自动重算。
QtObject {
    id: theme

    // ---- 深浅色开关 ----
    // 默认深色（历史观感）；main.cpp 注入 themeController 后跟随其状态。
    // 可写：QML 预览/测试可直接 `theme.dark = false` 切浅色。
    property bool dark: typeof themeController !== "undefined" ? themeController.dark : true

    // ---- 深色色板（原始设计，勿改：老用户观感基线）----
    readonly property var darkTokens: ({
        // 背景层
        "bg": "#090d16",
        "surface": "#0f172a",
        "panel": "#0f172a",
        "panelAlt": "#182338",
        "card": "#131d31",
        "inputBg": "#0b1322",

        // 自绘标题栏（无边框窗口）
        "titlebar": "#080b12",
        "titlebarInactive": "#0a0e17",
        "titlebarHover": "#1b2637",
        "titlebarPress": "#26344a",
        "titlebarClose": "#e81123",

        // 边框
        "border": "#1e293b",
        "borderLight": "#2a3b53",

        // 文本
        "text": "#e2e8f0",
        "textDim": "#94a3b8",
        "textFaint": "#64748b",

        // 语义色（统一约定：接收=emerald，发送=sky，错误=danger）
        "accent": "#38bdf8",
        "accentDim": "#0ea5e9",
        "accentInk": "#04121f",
        "rx": "#10b981",
        "tx": "#38bdf8",
        "warn": "#f59e0b",
        "error": "#ef4444",

        // 通道标识色
        "ch1": "#10b981",
        "ch2": "#f59e0b",

        // 斑马纹（表格隔行）
        "zebra": Qt.rgba(1, 1, 1, 0.015),

        // 滚动条（= 原 border / borderLight，保持深色观感不变）
        "scrollThumb": "#1e293b",
        "scrollThumbActive": "#2a3b53"
    })

    // ---- 浅色色板 ----
    // 语义色比深色压深一档：深色板里的 emerald/sky/amber 直接放到白底上对比度不足 2:1。
    // 注意 bg 不能取纯白：tst_panels 用「根背景 != #ffffff」检测面板漏绑 theme.bg。
    readonly property var lightTokens: ({
        // 背景层
        // 层次（亮→暗）：card/inputBg（纯白，最亮）> panel/surface（工具栏、左右栏）
        //   > bg（报文列表底）> panelAlt（表头、悬停、标题栏一带，最深）
        // 与深色板相反方向：浅色界面靠「背景更深 + 卡片纯白」拉出层次
        "bg": "#e8edf4",
        "surface": "#f4f7fb",
        "panel": "#f4f7fb",
        "panelAlt": "#e0e7f0",
        "card": "#ffffff",
        "inputBg": "#ffffff",

        // 自绘标题栏（无边框窗口）
        "titlebar": "#dfe6ef",
        "titlebarInactive": "#e6ebf3",
        "titlebarHover": "#d2dbe8",
        "titlebarPress": "#c3cede",
        "titlebarClose": "#e81123",

        // 边框
        "border": "#d3dce8",
        "borderLight": "#b6c4d6",

        // 文本
        "text": "#0f172a",
        "textDim": "#475569",
        "textFaint": "#64748b",

        // 语义色
        "accent": "#0284c7",
        "accentDim": "#0369a1",
        "accentInk": "#ffffff",
        "rx": "#059669",
        "tx": "#0284c7",
        "warn": "#b45309",
        "error": "#dc2626",

        // 通道标识色
        "ch1": "#059669",
        "ch2": "#b45309",

        // 斑马纹（表格隔行）
        "zebra": Qt.rgba(0.06, 0.09, 0.16, 0.04),

        // 滚动条
        "scrollThumb": "#a9b8ce",
        "scrollThumbActive": "#8798b0"
    })

    readonly property var tokens: dark ? darkTokens : lightTokens

    // ---- 背景层 ----
    readonly property color bg: tokens.bg
    readonly property color surface: tokens.surface
    readonly property color panel: tokens.panel
    readonly property color card: tokens.card
    readonly property color panelAlt: tokens.panelAlt
    readonly property color inputBg: tokens.inputBg

    // ---- 自绘标题栏（无边框窗口）----
    readonly property color titlebar: tokens.titlebar
    readonly property color titlebarInactive: tokens.titlebarInactive
    readonly property color titlebarHover: tokens.titlebarHover
    readonly property color titlebarPress: tokens.titlebarPress
    readonly property color titlebarClose: tokens.titlebarClose

    // ---- 边框 ----
    readonly property color border: tokens.border
    readonly property color borderLight: tokens.borderLight

    // ---- 文本 ----
    readonly property color text: tokens.text
    readonly property color textDim: tokens.textDim
    readonly property color textFaint: tokens.textFaint

    // ---- 语义色（统一约定：接收=emerald，发送=sky，错误=danger）----
    readonly property color accent: tokens.accent
    readonly property color accentDim: tokens.accentDim
    readonly property color rx: tokens.rx
    readonly property color tx: tokens.tx
    readonly property color warn: tokens.warn
    readonly property color error: tokens.error
    readonly property color accentInk: tokens.accentInk

    // ---- 表格斑马纹 ----
    readonly property color zebra: tokens.zebra

    // ---- 滚动条 ----
    // 深色下必须比背景亮一点、浅色下必须比背景暗一点，否则在表格/卡片底色上「隐形」。
    readonly property color scrollThumb: tokens.scrollThumb
    readonly property color scrollThumbActive: tokens.scrollThumbActive

    // ---- 通道标识色 ----
    readonly property color ch1: tokens.ch1
    readonly property color ch2: tokens.ch2

    // ---- Qt Quick Controls 2「Default 样式」调色板 ----
    // ToolTip / Dialog 按钮 / BusyIndicator / Overlay 遮罩 / ComboBox 弹层 / Slider
    // 取的是 control.palette.*，它在 Qt 5.15 里既不读 QGuiApplication 调色板、
    // 也不受 qApp->setPalette() 影响（实测），只能从 QML 写。
    // Main.qml 把这张表逐项绑到根 ApplicationWindow 的 palette 上，子控件自动继承。
    // 注意：Qt 5.15 的 QQuickPalette 没有 placeholderText 角色（写了会报
    // "Cannot assign to non-existent property"），输入框占位色由 AppTextField 自己指定。
    readonly property var controls: ({
        "window": tokens.panel,
        "windowText": tokens.text,
        "base": tokens.inputBg,
        "alternateBase": tokens.panelAlt,
        "text": tokens.text,
        "button": tokens.panelAlt,
        "buttonText": tokens.text,
        "brightText": "#ffffff",
        "highlight": tokens.accent,
        "highlightedText": tokens.accentInk,
        "toolTipBase": tokens.card,
        "toolTipText": tokens.text,
        // Default 样式的 BusyIndicator 圆环与 Dialog 边框都取 palette.dark：
        // 深色板下必须是中间调，否则圆环在深色卡片上看不见。
        "dark": tokens.textFaint,
        "mid": tokens.borderLight,
        "light": tokens.panelAlt,
        "shadow": "#000000"
    })

    // ---- 字体（未安装时回退通用族）----
    readonly property string monoFamily: "monospace"
    readonly property string sansFamily: "sans-serif"

    // ---- 尺寸 ----
    readonly property int rowHeight: 26
    readonly property int fontSize: 12
    readonly property int radius: 8
    readonly property int radiusSm: 5
    readonly property int spacing: 8

    // ---- 标题栏 / 无边框拖拽区尺寸 ----
    readonly property int titlebarHeight: 46
    readonly property int titlebarButtonWidth: 46
    readonly property int resizeMargin: 5
    readonly property int dragThreshold: 4

    function channelColor(index) {
        return index === 0 ? ch1 : (index === 1 ? ch2 : textDim)
    }

    // hover / pressed 的明暗方向随主题翻转：深色下「提亮」，浅色下「压暗」，
    // 否则浅色主题 hover 会把浅底按钮越变越白（等于没反馈）。
    function hoverTint(base) {
        return dark ? Qt.lighter(base, 1.12) : Qt.darker(base, 1.08)
    }

    function pressTint(base) {
        return dark ? Qt.darker(base, 1.2) : Qt.darker(base, 1.16)
    }
}
