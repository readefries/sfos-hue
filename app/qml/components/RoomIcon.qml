import QtQuick 2.0
import Sailfish.Silica 1.0

// The kind of room or zone, from its archetype; highlighted while any of its lights are on.
HighlightImage {
    property string archetype
    property bool on: true

    function kind(archetype) {
        if (/living|lounge|recreation|man_cave|tv|music|reading/.test(archetype))
            return "sofa"
        if (/kitchen|dining|barbecue/.test(archetype))
            return "kitchen"
        if (/bed|nursery|guest/.test(archetype))
            return "bed"
        if (/bath|toilet|laundry|pool/.test(archetype))
            return "bath"
        if (/office|computer|studio|gym/.test(archetype))
            return "desk"
        if (/hall|door|stair|closet|storage|garage|carport/.test(archetype))
            return "door"
        if (/garden|terrace|balcony|porch|driveway/.test(archetype))
            return "garden"
        return "home"
    }

    width: Theme.iconSizeMedium
    height: width
    sourceSize.width: width
    sourceSize.height: height
    source: Qt.resolvedUrl("../icons/room-" + kind(archetype) + ".svg")
    color: on ? Theme.highlightColor : Theme.secondaryColor
    opacity: on ? 1.0 : Theme.opacityHigh
}
