import QtQuick 2.0
import Sailfish.Silica 1.0
import "icons.js" as Icons

// The kind of room or zone, from its archetype; highlighted while any of its lights are on.
HighlightImage {
    property string archetype
    property bool on: true

    width: Theme.iconSizeMedium
    height: width
    sourceSize.width: width
    sourceSize.height: height
    source: Icons.room(archetype)
    color: on ? Theme.highlightColor : Theme.secondaryColor
    opacity: on ? 1.0 : Theme.opacityHigh
}
