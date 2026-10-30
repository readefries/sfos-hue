import QtQuick 2.0
import Sailfish.Silica 1.0

// Hue around, saturation outwards, at full value: brightness has its own slider.
Item {
    id: wheel

    // Where the marker sits; follows the lamp unless the finger is on the wheel.
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

    ShaderEffect {
        width: wheel.radius * 2
        height: width
        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform lowp float qt_Opacity;
            lowp vec3 hsv2rgb(lowp vec3 c) {
                lowp vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
                lowp vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
            }
            void main() {
                highp vec2 d = qt_TexCoord0 * 2.0 - 1.0;
                highp float r = length(d);
                highp float h = atan(d.y, d.x) / 6.28318530718 + 0.5;
                lowp float a = qt_Opacity * clamp((1.0 - r) * 80.0, 0.0, 1.0);
                gl_FragColor = vec4(hsv2rgb(vec3(h, min(r, 1.0), 1.0)) * a, a);
            }"
    }

    Rectangle {
        id: marker
        width: Theme.iconSizeSmall
        height: width
        radius: width / 2
        color: "transparent"
        border.width: Theme.paddingSmall / 2
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

        function pick(mouse) {
            var dx = mouse.x - wheel.radius
            var dy = mouse.y - wheel.radius
            var s = Math.min(Math.sqrt(dx * dx + dy * dy) / wheel.radius, 1)
            var h = Math.atan2(dy, dx) / (2 * Math.PI) + 0.5
            wheel.placeMarker(h, s)
            wheel.colorPicked(Qt.hsva(h, s, 1, 1))
        }

        onPressed: pick(mouse)
        onPositionChanged: pick(mouse)
    }
}
