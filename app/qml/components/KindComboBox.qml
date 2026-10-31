import QtQuick 2.0
import Sailfish.Silica 1.0
import "archetypes.js" as Archetypes

// Picks the kind (archetype) of a room or zone.
ComboBox {
    id: box

    property string archetype: "other"
    // What the box shows; an archetype it doesn't offer stays as it was unless changed here.
    readonly property string selected: changed ? kinds[currentIndex].id : archetype
    property bool changed: false
    readonly property var kinds: Archetypes.list()

    label: qsTr("Kind")
    Component.onCompleted: currentIndex = Archetypes.indexOf(archetype)
    onCurrentIndexChanged: if (currentIndex !== Archetypes.indexOf(archetype)) changed = true

    menu: ContextMenu {
        Repeater {
            model: box.kinds
            MenuItem {
                text: modelData.name
            }
        }
    }
}
