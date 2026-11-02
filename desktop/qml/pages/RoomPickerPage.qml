import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components"

// Picks the room a Zigbee light is in: one of the bridge's, or one of Lampje's own.
Kirigami.ScrollablePage {
    id: page

    property string lightId
    property string lightName
    readonly property string current: hue.roomOfLight(lightId)

    title: qsTr("Room") + " · " + lightName

    function choose(roomId) {
        hue.setLightRoom(lightId, roomId)
        applicationWindow().closeForm()
    }

    RoomModel {
        id: rooms
        store: hue.store
    }

    actions: [
        Kirigami.Action {
            text: qsTr("New room")
            icon.name: "list-add"
            onTriggered: {
                var form = applicationWindow().openForm(Qt.resolvedUrl("EditDialog.qml"), {
                                                            title: qsTr("New room"),
                                                            archetype: "office",
                                                            withKind: true
                                                        })
                form.accepted.connect(function() {
                    var roomId = hue.createRoom(form.newName, form.newArchetype)
                    if (roomId !== "")
                        page.choose(roomId)
                })
            }
        },
        Kirigami.Action {
            text: qsTr("Cancel")
            icon.name: "dialog-cancel"
            shortcut: "Escape"
            onTriggered: applicationWindow().closeForm()
        }
    ]

    ColumnLayout {
        spacing: 0

        QQC2.RadioDelegate {
            Layout.fillWidth: true
            text: qsTr("No room")
            checked: page.current === ""
            onClicked: page.choose("")
        }
        Repeater {
            model: rooms
            delegate: QQC2.RadioDelegate {
                Layout.fillWidth: true
                visible: model.kind === "room"
                text: model.name
                checked: page.current === model.resourceId
                onClicked: page.choose(model.resourceId)
            }
        }
    }
}
