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

    // 回归：单帧面板的「快捷周期发送」默认状态（周期 100ms / 次数 1 次 / 离线不运行）
    function test_sendPanelQuickPeriodicDefaults() {
        var theme = makeTheme()
        var c = loadPanel("SendPanel")
        var panel = c.createObject(testCase, { "theme": theme })
        verify(panel !== null, "SendPanel 实例化失败: " + c.errorString())

        compare(panel.quickIntervalMs, 100, "快捷周期默认周期应为 100ms")
        compare(panel.quickRepeat, 1, "快捷周期默认次数应为 1 次")
        compare(panel.quickRunning(), false, "离线时快捷周期不应处于运行态")
        verify(String(panel.statusText()).length > 0, "状态行应有文案")

        panel.destroy()
        theme.destroy()
    }

    // 次数留空/非法时按 1 次（与默认值一致），显式 0 才视为无限循环
    function test_quickPeriodicRepeatFallback() {
        var theme = makeTheme()
        var c = loadPanel("SendPanel")
        var panel = c.createObject(testCase, { "theme": theme })
        verify(panel !== null, "SendPanel 实例化失败: " + c.errorString())

        var field = findByName(panel, "quickRepeatField")
        verify(field !== null, "找不到次数输入框")

        field.text = ""
        panel.startQuick()
        compare(panel.quickRepeat, 1, "次数留空应回退为 1 次")

        field.text = "abc"
        panel.startQuick()
        compare(panel.quickRepeat, 1, "次数非法应回退为 1 次")

        field.text = "0"
        panel.startQuick()
        compare(panel.quickRepeat, 0, "次数 0 应表示无限循环")

        panel.destroy()
        theme.destroy()
    }

    // 回归：离线点「周期发送」必须被拒绝并给出原因，且不会把消息发给周期页签的通道状态
    function test_quickPeriodicRequiresDevice() {
        var theme = makeTheme()
        var c = loadPanel("SendPanel")
        var panel = c.createObject(testCase, { "theme": theme })
        verify(panel !== null, "SendPanel 实例化失败: " + c.errorString())

        var notices = []
        periodicSender.quickOperationFinished.connect(function (p, ok, msg) {
            notices.push({ "port": p, "ok": ok, "msg": String(msg) })
        })

        panel.startQuick()
        compare(notices.length, 1, "离线启动应立刻回一条失败通知")
        compare(notices[0].ok, false, "离线启动必须是失败")
        verify(notices[0].msg.length > 0, "失败通知应带原因")
        compare(panel.quickRunning(), false, "失败后不应处于运行态")
        compare(String(panel.quickNotice), notices[0].msg, "面板状态行应展示失败原因")

        panel.destroy()
        theme.destroy()
    }

    // 回归：单帧面板必须完整塞进底部发送区（页签 38 + 单帧页签高度 215 -> 可用 177）；
    // 本次改动的两行不得把面板既有最小宽度（第 1 行决定的 829）撑大
    function test_sendPanelFitsBottomWorkspace() {
        var theme = makeTheme()
        var c = loadPanel("SendPanel")
        // 默认窗口宽度 1440 - 左栏 286 = 1154
        var panel = c.createObject(testCase, { "theme": theme, "width": 1154, "height": 177 })
        verify(panel !== null, "SendPanel 实例化失败: " + c.errorString())

        var row = findByName(panel, "sendRow")
        var optionsRow = findByName(panel, "optionsRow")
        var quickButton = findByName(panel, "quickSendButton")
        var status = findByName(panel, "sendStatus")
        verify(row !== null, "找不到数据+发送行")
        verify(optionsRow !== null, "找不到选项行")
        verify(quickButton !== null, "找不到周期发送按钮")
        verify(status !== null, "找不到状态行")

        var right = quickButton.x + quickButton.width
        verify(right <= panel.width + 0.5,
               "周期发送按钮超出面板宽度: " + right + " > " + panel.width)
        // 面板既有最小宽度 829（第 1 行"帧参数"决定，与本次改动无关）；这两行不得更大
        verify(row.implicitWidth <= 829,
               "数据+发送行把面板最小宽度撑大了: " + row.implicitWidth + " > 829")
        verify(optionsRow.implicitWidth <= 829,
               "选项行把面板最小宽度撑大了: " + optionsRow.implicitWidth + " > 829")
        verify(status.y + status.height <= panel.height + 0.5,
               "状态行超出可视区: " + (status.y + status.height) + " > " + panel.height)

        panel.destroy()
        theme.destroy()
    }

    // 回归：底部发送区高度按页签区分（单帧页签收紧，周期/记录回放保持）
    function test_sendWorkspaceHeightPerTab() {
        var theme = makeTheme()
        var c = loadPanel("SendWorkspace")
        var ws = c.createObject(testCase, { "theme": theme, "width": 900, "height": 300 })
        verify(ws !== null, "SendWorkspace 实例化失败: " + c.errorString())

        ws.currentIndex = 0
        compare(ws.panelHeight, 215, "单帧页签应收紧到 215")
        ws.currentIndex = 1
        compare(ws.panelHeight, 256, "周期页签应保持 256")
        ws.currentIndex = 2
        compare(ws.panelHeight, 256, "记录/回放页签应保持 256")

        ws.destroy()
        theme.destroy()
    }

    // 回归：单帧面板的 specMap 必须带上 ID 递增开关（新增开关最容易漏传）
    function test_sendPanelSpecMapCarriesIdIncrement() {
        var theme = makeTheme()
        var c = loadPanel("SendPanel")
        var panel = c.createObject(testCase, { "theme": theme })
        verify(panel !== null, "SendPanel 实例化失败: " + c.errorString())

        var box = findByName(panel, "idIncrCheck")
        verify(box !== null, "找不到「ID 递增」勾选框")
        compare(panel.specMap().incrementId, false, "默认不应勾选 ID 递增")

        box.checked = true
        compare(panel.specMap().incrementId, true, "勾选后 specMap 必须带 incrementId")
        compare(panel.specMap().rolling, false, "不应顺带打开滚动计数")
        compare(panel.specMap().increment, false, "不应顺带打开递增填充")

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
