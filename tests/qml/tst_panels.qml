import QtQuick 2.15
import QtTest 1.15

TestCase {
    id: testCase
    name: "Panels"
    width: 800
    height: 600

    function makeTheme() {
        var c = Qt.createComponent("qrc:/qml/Theme.qml")
        compare(c.status, Component.Ready, "Theme: " + c.errorString())
        return c.createObject(testCase)
    }

    function loadPanel(name) {
        var c = Qt.createComponent("qrc:/qml/" + name + ".qml")
        compare(c.status, Component.Ready, name + ": " + c.errorString())
        return c
    }

    function test_allQmlCompile() {
        var names = ["Main", "Theme", "DevicePanel", "ReceiveView", "SendPanel",
                     "PeriodicPanel", "StatisticsPanel", "RecordPanel", "SendWorkspace"]
        for (var i = 0; i < names.length; ++i) {
            var c = Qt.createComponent("qrc:/qml/" + names[i] + ".qml")
            compare(c.status, Component.Ready, names[i] + ": " + c.errorString())
        }

        var comps = ["AppButton", "AppCard", "AppCheckBox", "AppComboBox",
                     "AppRadioButton", "AppTextField", "ChannelBadge", "ChannelTab", "Icon",
                     "MetricCard", "SectionHeader", "SlimScrollBar", "StatusDot",
                     "ToolbarButton", "WindowButton"]
        for (var j = 0; j < comps.length; ++j) {
            var cc = Qt.createComponent("qrc:/qml/components/" + comps[j] + ".qml")
            compare(cc.status, Component.Ready, comps[j] + ": " + cc.errorString())
        }
    }

    function test_instantiatePanels() {
        var names = ["DevicePanel", "ReceiveView", "SendPanel",
                     "PeriodicPanel", "StatisticsPanel", "RecordPanel", "SendWorkspace"]
        var theme = makeTheme()
        verify(theme !== null)

        for (var i = 0; i < names.length; ++i) {
            var c = loadPanel(names[i])
            var obj = c.createObject(testCase, { "theme": theme })
            verify(obj !== null, names[i] + " 实例化失败: " + c.errorString())
            obj.destroy()
        }
        theme.destroy()
    }

    // 回归：面板根节点必须显式绑定主题背景色。
    // QML Rectangle 默认 color 为 #ffffff，一旦漏掉 `color: theme.bg`，整个面板会变纯白。
    function test_panelRootBackground() {
        var theme = makeTheme()
        verify(theme !== null)

        var names = ["SendPanel", "PeriodicPanel", "RecordPanel"]
        for (var i = 0; i < names.length; ++i) {
            var c = loadPanel(names[i])
            var obj = c.createObject(testCase, { "theme": theme })
            verify(obj !== null, names[i] + " 实例化失败: " + c.errorString())

            var actual = String(obj.color)
            verify(actual !== "#ffffff", names[i] + " 根背景为默认白（漏绑定 theme.bg？）")
            compare(actual, String(theme.bg), names[i] + " 根背景应为 theme.bg")
            obj.destroy()
        }
        theme.destroy()
    }

    // 回归：通道标签页的「选中」必须明显区别于未选中（着色底 + 着色边框 + 通道色文字）
    function test_channelTabSelectionIsDistinct() {
        var theme = makeTheme()
        var c = Qt.createComponent("qrc:/qml/components/ChannelTab.qml")
        compare(c.status, Component.Ready, "ChannelTab: " + c.errorString())

        var off = c.createObject(testCase, { "theme": theme, "channel": 0, "selected": false, "text": "通道 0" })
        var on = c.createObject(testCase, { "theme": theme, "channel": 1, "selected": true, "text": "通道 1" })
        verify(off !== null && on !== null, "ChannelTab 实例化失败")

        // 未选中：无着色背景
        compare(off.background.color.a, 0, "未选中的通道标签不应有着色背景")
        // 选中：明显着色背景 + 着色边框
        verify(on.background.color.a > 0.1, "选中的通道标签必须有明显的着色背景")
        verify(on.background.border.color.a > 0.5, "选中的通道标签边框应着色")
        // 选中文字用通道色
        var onText = findByName(on.contentItem, "label")
        verify(onText !== null, "找不到通道标签文字")
        compare(String(onText.color), String(theme.channelColor(1)), "选中文字应为通道色")
        // 运行态圆点：实心
        var dot = findByName(on.contentItem, "dot")
        verify(dot !== null, "找不到状态圆点")
        compare(dot.color.a, 0, "未运行时圆点应为空心")
        on.running = true
        verify(dot.color.a > 0.5, "运行中圆点应为实心")

        off.destroy()
        on.destroy()
        theme.destroy()
    }

    // 回归：周期发送必须默认「正常模式」(0)，而不是自发自收 (2)。
    // 曾硬编码 txm=2，导致周期帧只在本机回环、不按正常模式上总线。
    function test_periodicEntryDefaults() {
        var theme = makeTheme()
        var c = loadPanel("PeriodicPanel")
        var panel = c.createObject(testCase, { "theme": theme })
        verify(panel !== null, "PeriodicPanel 实例化失败: " + c.errorString())

        panel.addEntry()
        var e = panel.entriesArray()
        compare(e.length, 1, "应生成 1 条周期项")
        compare(e[0].txm, 0, "周期条目发送模式应为「正常」(0)")
        compare(e[0].echo, true, "应保留发送回显")
        compare(e[0].intervalMs, 100, "默认周期 100ms")

        panel.destroy()
        theme.destroy()
    }

    // 按 objectName 递归查找（不依赖 QtTest 的 findChild 实现）
    function findByName(root, name) {
        if (!root || !root.children)
            return null
        for (var i = 0; i < root.children.length; ++i) {
            var ch = root.children[i]
            if (ch.objectName === name)
                return ch
            var hit = findByName(ch, name)
            if (hit)
                return hit
        }
        return null
    }

    // 回归：位定时文本必须对每个波特率组合都渲染出「当前」速率。
    // 早前该绑定里写 QString() 会抛 ReferenceError（QML 运行时没有 QString），
    // 结果是文本保留旧值并被永久冻结（绑定一旦抛异常就不再重算）。
    function test_bitTimingTextForAllRates() {
        var theme = makeTheme()
        var c = loadPanel("DevicePanel")
        var panel = c.createObject(testCase, { "theme": theme })
        verify(panel !== null, "DevicePanel 实例化失败: " + c.errorString())

        var card = findByName(panel, "channelCard_0")
        verify(card !== null, "找不到通道卡片")
        var text = findByName(panel, "bitTimingText_0")
        verify(text !== null, "找不到位定时文本")

        for (var a = 0; a < panel.arbRates.length; ++a) {
            for (var d = 0; d < panel.dataRates.length; ++d) {
                card.arbIndex = a
                card.dataIndex = d
                var body = String(text.text)
                var label = "仲裁 " + panel.arbRates[a] + " / 数据 " + panel.dataRates[d]
                // 必须包含当前速率本身，而不是残留的上一次速率
                verify(body.indexOf(String(panel.arbRates[a])) >= 0,
                       label + " 位定时文本未同步: " + body)
            }
        }

        panel.destroy()
        theme.destroy()
    }
}
