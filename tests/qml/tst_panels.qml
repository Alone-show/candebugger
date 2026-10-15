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
                     "AppRadioButton", "AppTextField", "ChannelBadge", "Icon",
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
