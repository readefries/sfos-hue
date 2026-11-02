import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components"

// Picks the room a Zigbee light is in: one of the bridge's, or one of Lampje's own.
Page {
    id: page

    property string lightId
    property string lightName
    readonly property string current: hue.roomOfLight(lightId)

    function choose(roomId) {
        hue.setLightRoom(lightId, roomId)
        pageStack.pop()
    }

    RoomModel {
        id: rooms
        store: hue.store
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("New room")
                onClicked: {
                    var dialog = pageStack.push(Qt.resolvedUrl("EditDialog.qml"), {
                                                    title: qsTr("New room"),
                                                    archetype: "office",
                                                    withKind: true,
                                                    // Straight back to the light.
                                                    acceptDestination: pageStack.previousPage(page),
                                                    acceptDestinationAction: PageStackAction.Pop
                                                })
                    dialog.accepted.connect(function() {
                        var roomId = hue.createRoom(dialog.newName, dialog.newArchetype)
                        if (roomId !== "")
                            hue.setLightRoom(page.lightId, roomId)
                    })
                }
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Room")
                description: page.lightName
            }

            Choice {
                text: qsTr("No room")
                selected: page.current === ""
                onClicked: page.choose("")
            }
            Repeater {
                model: rooms
                delegate: Choice {
                    visible: model.kind === "room"
                    text: model.name
                    archetype: model.archetype
                    selected: page.current === model.resourceId
                    onClicked: page.choose(model.resourceId)
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
