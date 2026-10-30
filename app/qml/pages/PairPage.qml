import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0

Page {
    id: page

    property string host
    property string bridgeId
    property string bridgeName

    readonly property bool failed: hue.pairingState === HueClient.PairingFailed

    Component.onCompleted: hue.pair(host, bridgeId, bridgeName)
    Component.onDestruction: hue.cancelPairing()

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        PageHeader {
            title: page.bridgeName
            description: page.host
        }

        Item {
            width: parent.width
            height: Theme.itemSizeHuge * 1.5

            ProgressCircle {
                anchors.centerIn: parent
                width: Theme.itemSizeHuge
                height: width
                visible: !page.failed
                value: hue.pairingSecondsLeft / 60
            }
            Label {
                anchors.centerIn: parent
                visible: !page.failed
                text: hue.pairingSecondsLeft
                font.pixelSize: Theme.fontSizeExtraLarge
                color: Theme.highlightColor
            }
            Image {
                anchors.centerIn: parent
                visible: page.failed
                source: "image://theme/icon-l-attention?" + Theme.highlightColor
            }
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.highlightColor
            text: page.failed ? hue.pairingError
                              : qsTr("Press the round button on top of the bridge")
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.failed
            text: qsTr("Try again")
            onClicked: hue.pair(page.host, page.bridgeId, page.bridgeName)
        }
    }
}
