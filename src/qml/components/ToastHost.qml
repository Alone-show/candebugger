import QtQuick 2.15

// 发送失败横幅宿主：堆叠多条 Toast，按「同文案」在 coalesceMs 内合并计数，
// 并限制同屏条数上限，避免周期发送失败时刷屏。
// 由 Main.qml 锚定在发送区上方；show() 由各发送失败信号驱动。
Column {
    id: host
    property var theme
    property int timeoutMs: 4000
    property int coalesceMs: 2500
    property int maxVisible: 3
    spacing: 6

    // 当前在屏的 Toast（内部状态，测试可读）
    property var items: []

    function itemCount() { return items.length }
    function messageOf(i) { return items[i] ? items[i].message : "" }
    function countOf(i) { return items[i] ? items[i].count : 0 }

    function show(message, severity) {
        if (message === undefined || message === null || message.length === 0)
            return
        if (!severity)
            severity = "error"

        var now = Date.now()
        for (var i = 0; i < items.length; ++i) {
            var t = items[i]
            if (t.severity === severity && t.message === message
                    && (now - t.lastAt) < coalesceMs) {
                t.lastAt = now
                t.count += 1
                t.kick()
                return
            }
        }

        while (items.length >= maxVisible)
            removeAt(0)

        var comp = Qt.createComponent("Toast.qml")
        if (comp.status !== Component.Ready) {
            console.warn("Toast.qml 无法加载:", comp.errorString())
            return
        }
        var obj = comp.createObject(host, {
            "theme": host.theme,
            "message": message,
            "severity": severity,
            "timeoutMs": host.timeoutMs
        })
        if (!obj)
            return
        obj.lastAt = now
        obj.dismissed.connect(function() { host.removeToast(obj) })
        items.push(obj)
    }

    function removeToast(obj) {
        var idx = items.indexOf(obj)
        if (idx < 0)
            return
        items.splice(idx, 1)
        obj.destroy()
    }

    function removeAt(i) {
        if (i >= 0 && i < items.length)
            removeToast(items[i])
    }

    function clear() {
        while (items.length > 0)
            removeAt(0)
    }
}
