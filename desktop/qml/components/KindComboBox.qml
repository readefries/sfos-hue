import QtQuick
import QtQuick.Controls as QQC2
import "archetypes.js" as Archetypes

// Picks the kind (archetype) of a room or zone.
QQC2.ComboBox {
    id: box

    property string archetype: "other"
    // What the box shows; an archetype it doesn't offer stays as it was unless changed here.
    readonly property string selected: changed ? kinds[currentIndex].id : archetype
    property bool changed: false
    readonly property var kinds: Archetypes.list()

    model: kinds
    textRole: "name"
    Component.onCompleted: currentIndex = Archetypes.indexOf(archetype)
    onActivated: changed = true
}
