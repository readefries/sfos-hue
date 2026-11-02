import QtQuick
import org.kde.kirigami as Kirigami

// Hue around, saturation outwards, at full value: brightness has its own slider. Same angles as
// the phone's wheel, drawn on a canvas (the phone's GLSL shader would need compiling for Qt 6).
Item {
    id: wheel

    // Where the marker sits; follows the lamp unless the mouse is on the wheel.
    property color currentColor: "white"
    signal colorPicked(color color)

    readonly property real radius: Math.min(width, height) / 2

    function hsvOf(c) {
        var max = Math.max(c.r, c.g, c.b)
        var min = Math.min(c.r, c.g, c.b)
        var d = max - min
        var h = 0
        if (d > 0) {
            if (max === c.r)
                h = ((c.g - c.b) / d) % 6
            else if (max === c.g)
                h = (c.b - c.r) / d + 2
            else
                h = (c.r - c.g) / d + 4
            h /= 6
            if (h < 0)
                h += 1
        }
        return { h: h, s: max > 0 ? d / max : 0 }
    }

    function placeMarker(h, s) {
        var angle = (h - 0.5) * 2 * Math.PI
        marker.x = radius + Math.cos(angle) * s * radius - marker.width / 2
        marker.y = radius + Math.sin(angle) * s * radius - marker.height / 2
    }

    function follow() {
        if (!touch.pressed) {
            var hsv = hsvOf(currentColor)
            placeMarker(hsv.h, hsv.s)
        }
    }

    onCurrentColorChanged: follow()
    onRadiusChanged: follow()

    Canvas {
        id: canvas
        width: wheel.radius * 2
        height: width
        onWidthChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            var r = width / 2
            ctx.reset()
            // Hue h sits at angle (h - 0.5) turns, clockwise from the right, as in placeMarker().
            // Canvas conical gradients run counter-clockwise, from the angle given.
            var hues = ctx.createConicalGradient(r, r, 0)
            for (var i = 0; i <= 12; ++i) {
                var stop = i / 12
                hues.addColorStop(stop, Qt.hsva((1.5 - stop) % 1, 1, 1, 1))
            }
            ctx.beginPath()
            ctx.arc(r, r, r, 0, 2 * Math.PI)
            ctx.fillStyle = hues
            ctx.fill()
            var white = ctx.createRadialGradient(r, r, 0, r, r, r)
            white.addColorStop(0, "#ffffffff")
            white.addColorStop(1, "#00ffffff")
            ctx.fillStyle = white
            ctx.fill()
        }
    }

    Rectangle {
        id: marker
        width: Kirigami.Units.iconSizes.small
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: "black"
        Rectangle {
            anchors.fill: parent
            anchors.margins: parent.border.width
            radius: width / 2
            color: "transparent"
            border.width: parent.border.width
            border.color: "white"
        }
    }

    MouseArea {
        id: touch
        anchors.fill: parent
        preventStealing: true
        cursorShape: Qt.CrossCursor

        function pick(mouse) {
            var dx = mouse.x - wheel.radius
            var dy = mouse.y - wheel.radius
            var s = Math.min(Math.sqrt(dx * dx + dy * dy) / wheel.radius, 1)
            var h = Math.atan2(dy, dx) / (2 * Math.PI) + 0.5
            wheel.placeMarker(h, s)
            wheel.colorPicked(Qt.hsva(h, s, 1, 1))
        }

        onPressed: (mouse) => pick(mouse)
        onPositionChanged: (mouse) => pick(mouse)
    }
}
