import QtQuick 2.15

// 轻量矢量图标：16x16 坐标系，路径为 SVG path（绝对命令 M/L/H/V/A/Q/Z）。
// 使用 Canvas 绘制，避免依赖 QtQuick.Shapes。
Item {
    id: icon
    property string name: ""
    property color color: "#94a3b8"
    property real strokeWidth: 1.5

    implicitWidth: 16
    implicitHeight: 16

    readonly property var defaultSpec: ({ "path": "", "fill": false })
    readonly property var spec: paths[name] !== undefined ? paths[name] : defaultSpec

    readonly property var paths: ({
        "pause":        { "path": "M5.5 3 L5.5 13 M10.5 3 L10.5 13", "fill": false },
        "play":         { "path": "M5 3 L13 8 L5 13 Z", "fill": true },
        "stop":         { "path": "M4 4 H12 V12 H4 Z", "fill": true },
        "clear":        { "path": "M4 5 H12 M6.5 5 V13 M9.5 5 V13 M5 3.5 H11", "fill": false },
        "save":         { "path": "M8 3 V10 M5 7 L8 10 L11 7 M4 12.5 H12", "fill": false },
        "search":       { "path": "M7 3 A4 4 0 1 0 7 11 A4 4 0 1 0 7 3 M10 10 L13 13", "fill": false },
        "filter":       { "path": "M3 4 H13 L9 9 V12 L7 13 V9 Z", "fill": false },
        "send":         { "path": "M3 8 L13 3 L10 13 L8 9 Z", "fill": true },
        "refresh":      { "path": "M13 8 A5 5 0 1 1 10.5 3.8 M10.5 3 V6 H13", "fill": false },
        "plug":         { "path": "M6 2.5 V5.5 M10 2.5 V5.5 M4 5.5 H12 V8 A4 4 0 0 1 4 8 Z M8 12 V14.5", "fill": false },
        "check":        { "path": "M3 8 L6.5 11.5 L13 4.5", "fill": false },
        "warning":      { "path": "M8 2.5 L14.5 13.5 H1.5 Z M8 6.5 V9.5 M8 11.4 V11.5", "fill": false },
        "settings":     { "path": "M8 6 A2 2 0 1 0 8 10 A2 2 0 1 0 8 6 M8 1.5 V3.5 M8 12.5 V14.5 M1.5 8 H3.5 M12.5 8 H14.5 M3.5 3.5 L5 5 M11 11 L12.5 12.5 M12.5 3.5 L11 5 M5 11 L3.5 12.5", "fill": false },
        "help":         { "path": "M8 2 A6 6 0 1 0 8 14 A6 6 0 1 0 8 2 M6 6.2 A2 2 0 1 1 8.6 8 Q8 9 8 10 M8 12 V12.2", "fill": false },
        "add":          { "path": "M8 3 V13 M3 8 H13", "fill": false },
        "remove":       { "path": "M3 8 H13", "fill": false },
        "chevronDown":  { "path": "M4 6 L8 10 L12 6", "fill": false },
        "chevronUp":    { "path": "M4 10 L8 6 L12 10", "fill": false },
        "clock":        { "path": "M8 2 A6 6 0 1 0 8 14 A6 6 0 1 0 8 2 M8 5 V8 L10.5 10", "fill": false },
        "device":       { "path": "M8 2 A6 6 0 1 0 8 14 A6 6 0 1 0 8 2 M2 8 H14", "fill": false },
        "chart":        { "path": "M3 13 V8 M7 13 V4 M11 13 V6 M2 13 H14", "fill": false },
        "record":       { "path": "M8 3 A5 5 0 1 0 8 13 A5 5 0 1 0 8 3", "fill": false },
        "loop":         { "path": "M4 6 H11 A2 2 0 0 1 11 10 H5 M4 6 L6 4 M4 6 L6 8 M12 10 L10 8 M12 10 L10 12", "fill": false },
        "link":         { "path": "M6.5 9.5 L9.5 6.5 M5.5 7 L4 8.5 A2.2 2.2 0 0 0 7 11.5 L8.5 10 M10.5 9 L12 7.5 A2.2 2.2 0 0 0 9 4.5 L7.5 6", "fill": false }
    })

    onNameChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()
    onStrokeWidthChanged: canvas.requestPaint()

    function arcTo(ctx, x0, y0, rx, ry, rotDeg, largeArc, sweep, x, y) {
        if (rx === 0 || ry === 0) {
            ctx.lineTo(x, y)
            return
        }
        var phi = rotDeg * Math.PI / 180
        var sinPhi = Math.sin(phi)
        var cosPhi = Math.cos(phi)
        var dx = (x0 - x) / 2
        var dy = (y0 - y) / 2
        var x1p = cosPhi * dx + sinPhi * dy
        var y1p = -sinPhi * dx + cosPhi * dy
        rx = Math.abs(rx)
        ry = Math.abs(ry)
        var lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
        if (lambda > 1) {
            var sc = Math.sqrt(lambda)
            rx *= sc
            ry *= sc
        }
        var sign = (largeArc !== sweep) ? 1 : -1
        var numr = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
        var denr = rx * rx * y1p * y1p + ry * ry * x1p * x1p
        var co = sign * Math.sqrt(Math.max(0, numr / denr))
        var cxp = co * (rx * y1p / ry)
        var cyp = co * (-ry * x1p / rx)
        var cx = cosPhi * cxp - sinPhi * cyp + (x0 + x) / 2
        var cy = sinPhi * cxp + cosPhi * cyp + (y0 + y) / 2
        var theta1 = Math.atan2((y1p - cyp) / ry, (x1p - cxp) / rx)
        var theta2 = Math.atan2((-y1p - cyp) / ry, (-x1p - cxp) / rx)
        var dTheta = theta2 - theta1
        if (!sweep && dTheta > 0)
            dTheta -= 2 * Math.PI
        if (sweep && dTheta < 0)
            dTheta += 2 * Math.PI
        ctx.save()
        ctx.translate(cx, cy)
        ctx.rotate(phi)
        ctx.scale(rx, ry)
        ctx.arc(0, 0, 1, theta1, theta1 + dTheta, !sweep)
        ctx.restore()
    }

    function drawPath(ctx, d, filled) {
        var re = /([MLHVAQZmlhvaqz])|(-?\d*\.?\d+)/g
        var tokens = []
        var m
        while ((m = re.exec(d)) !== null) {
            if (m[1] !== undefined)
                tokens.push(m[1])
            else
                tokens.push(parseFloat(m[2]))
        }
        var i = 0
        var cx = 0
        var cy = 0
        var sx = 0
        var sy = 0
        var cmd = ""
        ctx.beginPath()
        while (i < tokens.length) {
            var t = tokens[i]
            if (typeof t === "string") {
                cmd = t
                i++
            }
            switch (cmd) {
            case "M":
                cx = tokens[i++]; cy = tokens[i++]
                ctx.moveTo(cx, cy); sx = cx; sy = cy
                cmd = "L"
                break
            case "L":
                cx = tokens[i++]; cy = tokens[i++]
                ctx.lineTo(cx, cy)
                break
            case "H":
                cx = tokens[i++]
                ctx.lineTo(cx, cy)
                break
            case "V":
                cy = tokens[i++]
                ctx.lineTo(cx, cy)
                break
            case "Q": {
                var qx = tokens[i++]; var qy = tokens[i++]
                var qex = tokens[i++]; var qey = tokens[i++]
                ctx.quadraticCurveTo(qx, qy, qex, qey)
                cx = qex; cy = qey
                break
            }
            case "A": {
                var arx = tokens[i++]; var ary = tokens[i++]
                var arot = tokens[i++]; var alarge = tokens[i++]; var asweep = tokens[i++]
                var aex = tokens[i++]; var aey = tokens[i++]
                arcTo(ctx, cx, cy, arx, ary, arot, alarge, asweep, aex, aey)
                cx = aex; cy = aey
                break
            }
            case "Z":
                ctx.closePath()
                cx = sx; cy = sy
                break
            default:
                i++
                break
            }
        }
        if (filled)
            ctx.fill()
        else
            ctx.stroke()
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var s = width / 16
            ctx.setTransform(s, 0, 0, s, 0, 0)
            ctx.strokeStyle = icon.color
            ctx.fillStyle = icon.color
            ctx.lineWidth = icon.spec.fill ? 0 : icon.strokeWidth
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            if (icon.spec.path !== "")
                icon.drawPath(ctx, icon.spec.path, icon.spec.fill)
        }
    }
}
