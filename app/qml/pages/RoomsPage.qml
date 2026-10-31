import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components"

Page {
    id: page

    RoomModel {
        id: rooms
        store: hue.store
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: rooms

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Sensors and switches")
                onClicked: pageStack.push(Qt.resolvedUrl("SensorsPage.qml"))
            }
            MenuItem {
                text: qsTr("New zone")
                enabled: hue.state === HueClient.Connected
                onClicked: pageStack.push(Qt.resolvedUrl("ZoneDialog.qml"))
            }
            MenuItem {
                text: qsTr("All lights off")
                enabled: hue.state === HueClient.Connected
                onClicked: hue.allOff()
            }
        }

        header: PageHeader {
            title: hue.bridgeName || "Lampje"
            description: {
                switch (hue.state) {
                case HueClient.Connecting: return qsTr("Connecting…")
                case HueClient.Reconnecting: return hue.lastError || qsTr("Reconnecting…")
                case HueClient.Unauthorized:
                case HueClient.Untrusted: return hue.lastError
                default: return ""
                }
            }
        }

        section {
            property: "kind"
            delegate: SectionHeader {
                text: section === "zone" ? qsTr("Zones") : qsTr("Rooms")
            }
        }

        delegate: GroupDelegate {}

        ViewPlaceholder {
            enabled: rooms.count === 0 && hue.state === HueClient.Connected
            text: qsTr("No rooms yet")
            hintText: qsTr("Rooms and zones are set up in the Hue app")
        }

        ViewPlaceholder {
            enabled: hue.state === HueClient.Unauthorized || hue.state === HueClient.Untrusted
            text: hue.lastError
            hintText: qsTr("Pull down for settings")
        }

        VerticalScrollDecorator {}
    }

    PageBusyIndicator {
        running: rooms.count === 0
                 && (hue.state === HueClient.Connecting || hue.state === HueClient.Reconnecting)
    }
}
