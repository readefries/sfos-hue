import QtQuick 2.0
import Sailfish.Silica 1.0

// The kind of lamp (from its archetype), in the colour it's showing; dimmed when off.
HighlightImage {
    id: icon

    property string archetype
    property color lightColor: "white"
    property bool on: true

    function kind(archetype) {
        if (/spot|recessed|flood/.test(archetype))
            return "spot"
        if (/^floor/.test(archetype))
            return "floor"
        if (/^table|desk|^wall/.test(archetype))
            return "table"
        if (/^pendant/.test(archetype))
            return "pendant"
        if (/^ceiling/.test(archetype))
            return "ceiling"
        if (/strip|play|bar|tube|gradient/.test(archetype))
            return "strip"
        return "bulb"
    }

    width: Theme.iconSizeMedium
    height: width
    sourceSize.width: width
    sourceSize.height: height
    source: Qt.resolvedUrl("../icons/light-" + kind(archetype) + ".svg")
    color: on ? lightColor : Theme.secondaryColor
    opacity: on ? 1.0 : Theme.opacityLow
}
