import QtQuick 2.0
import Sailfish.Silica 1.0
import "icons.js" as Icons

// The kind of lamp (from its archetype), in the colour it's showing; dimmed when off.
HighlightImage {
    id: icon

    property string archetype
    property color lightColor: "white"
    property bool on: true

    width: Theme.iconSizeMedium
    height: width
    sourceSize.width: width
    sourceSize.height: height
    source: Icons.light(archetype)
    color: on ? lightColor : Theme.secondaryColor
    opacity: on ? 1.0 : Theme.opacityLow
}
