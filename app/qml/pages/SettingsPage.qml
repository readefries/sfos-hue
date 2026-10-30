import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0

Page {
    id: page

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("Bridge")
            }
            DetailItem {
                label: qsTr("Name")
                value: hue.bridgeName
            }
            DetailItem {
                label: qsTr("Address")
                value: hue.bridgeHost
            }
            DetailItem {
                label: qsTr("Status")
                value: {
                    switch (hue.state) {
                    case HueClient.Connected: return qsTr("Connected")
                    case HueClient.Connecting: return qsTr("Connecting")
                    case HueClient.Reconnecting: return qsTr("Waiting to reconnect")
                    case HueClient.Unauthorized: return qsTr("Not paired")
                    case HueClient.Untrusted: return qsTr("Certificate changed")
                    default: return ""
                    }
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: hue.lastError !== "" && hue.state !== HueClient.Connected
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.errorColor
                text: hue.lastError
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            ButtonLayout {
                width: parent.width
                Button {
                    text: qsTr("Reconnect")
                    onClicked: hue.reconnect()
                }
                Button {
                    text: qsTr("Forget bridge")
                    onClicked: remorse.execute(qsTr("Forgetting bridge"), function() { hue.forget() })
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Forgetting only removes the bridge from this phone. To revoke the app's access, remove \"lampje#sailfishos\" in the Hue app.")
            }
        }

        VerticalScrollDecorator {}
    }
}
