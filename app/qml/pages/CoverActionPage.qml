import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components"

// Picks what one of the cover's buttons does.
Page {
    id: page

    property int slot
    readonly property var current: hue.coverActions[slot] || ({ kind: "none" })

    function choose(kind, id) {
        hue.setCoverAction(slot, kind, id || "")
        pageStack.pop()
    }

    RoomModel {
        id: rooms
        store: hue.store
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: page.slot === 0 ? qsTr("Left cover button") : qsTr("Right cover button")
            }

            Choice {
                text: qsTr("Nothing")
                selected: page.current.kind === "none"
                onClicked: page.choose("none")
            }
            Choice {
                text: qsTr("All lights off")
                selected: page.current.kind === "alloff"
                onClicked: page.choose("alloff")
            }

            SectionHeader {
                text: qsTr("Switch on or off")
            }
            Repeater {
                model: rooms
                delegate: Choice {
                    text: model.name
                    archetype: model.archetype
                    selected: page.current.kind === "group" && page.current.id === model.resourceId
                    onClicked: page.choose("group", model.resourceId)
                }
            }

            SectionHeader {
                text: qsTr("Scenes")
            }
            Repeater {
                model: rooms
                delegate: Column {
                    width: column.width
                    property string roomName: model.name

                    Repeater {
                        model: SceneModel {
                            store: hue.store
                            groupId: model.resourceId
                        }
                        delegate: Choice {
                            text: model.name
                            description: roomName
                            selected: page.current.kind === "scene" && page.current.id === model.resourceId
                            onClicked: page.choose("scene", model.resourceId)
                        }
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
