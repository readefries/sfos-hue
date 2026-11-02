import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components"

Kirigami.ScrollablePage {
    id: page

    title: hue.bridgeName || "Lampje"

    function open(file, properties) {
        applicationWindow().openAfter(page, Qt.resolvedUrl(file), properties)
    }

    RoomModel {
        id: rooms
        store: hue.store
    }

    actions: [
        Kirigami.Action {
            text: qsTr("All lights off")
            icon.name: "system-shutdown"
            enabled: hue.state === HueClient.Connected
            onTriggered: hue.allOff()
        },
        Kirigami.Action {
            text: qsTr("New zone")
            icon.name: "list-add"
            enabled: hue.state === HueClient.Connected
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: applicationWindow().openForm(Qt.resolvedUrl("ZoneDialog.qml"))
        },
        Kirigami.Action {
            text: qsTr("New room")
            icon.name: "list-add"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: {
                var form = applicationWindow().openForm(Qt.resolvedUrl("EditDialog.qml"), {
                                                            title: qsTr("New room"),
                                                            archetype: "other",
                                                            withKind: true
                                                        })
                form.accepted.connect(function() {
                    hue.createRoom(form.newName, form.newArchetype)
                })
            }
        },
        Kirigami.Action {
            text: qsTr("Sensors and switches")
            icon.name: "sensors-applet"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.open("SensorsPage.qml")
        },
        Kirigami.Action {
            text: qsTr("Schedules")
            icon.name: "chronometer"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.open("SchedulesPage.qml")
        },
        Kirigami.Action {
            text: qsTr("Settings")
            icon.name: "configure"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.open("SettingsPage.qml")
        }
    ]

    header: Kirigami.InlineMessage {
        width: parent ? parent.width : 0
        position: Kirigami.InlineMessage.Position.Header
        type: hue.state === HueClient.Unauthorized || hue.state === HueClient.Untrusted
              ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
        visible: text !== ""
        text: {
            switch (hue.state) {
            case HueClient.Connecting: return qsTr("Connecting…")
            case HueClient.Reconnecting: return hue.lastError || qsTr("Reconnecting…")
            case HueClient.Unauthorized:
            case HueClient.Untrusted: return hue.lastError
            default: return ""
            }
        }
    }

    ListView {
        id: list
        model: rooms

        section {
            property: "kind"
            delegate: Kirigami.ListSectionHeader {
                width: ListView.view.width
                text: section === "zone" ? qsTr("Zones") : qsTr("Rooms")
            }
        }

        delegate: GroupDelegate {
            onClicked: page.open("RoomPage.qml", {
                                     groupId: model.resourceId,
                                     groupName: model.name,
                                     groupKind: model.kind
                                 })
        }

        // Zigbee lights nobody put in a room yet; their page has a room picker.
        footer: ColumnLayout {
            width: list.width
            spacing: 0

            Kirigami.ListSectionHeader {
                Layout.fillWidth: true
                visible: unplaced.count > 0
                text: qsTr("Not in a room")
            }
            Repeater {
                model: LightModel {
                    id: unplaced
                    store: hue.store
                    withoutRoom: true
                }
                delegate: LightDelegate {
                    Layout.fillWidth: true
                    onClicked: page.open("LightPage.qml", { lightId: model.resourceId, lightName: model.name })
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: rooms.count === 0 && hue.state === HueClient.Connected
            text: qsTr("No rooms yet")
            explanation: qsTr("Rooms and zones are set up in the Hue app")
        }

        Kirigami.LoadingPlaceholder {
            anchors.centerIn: parent
            visible: rooms.count === 0
                     && (hue.state === HueClient.Connecting || hue.state === HueClient.Reconnecting)
        }
    }
}
