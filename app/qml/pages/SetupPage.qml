import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0

Page {
    id: page

    property string probingHost

    function bridgeAt(host) {
        var bridges = hue.discovery.bridges
        for (var i = 0; i < bridges.length; ++i) {
            if (bridges[i].host === host)
                return bridges[i]
        }
        return null
    }

    function openPairing(bridge) {
        pageStack.push(Qt.resolvedUrl("PairPage.qml"), {
                           host: bridge.host,
                           bridgeId: bridge.id,
                           bridgeName: bridge.name
                       })
    }

    function useAddress(host) {
        manualError.text = ""
        var known = bridgeAt(host)
        if (known) {
            openPairing(known)
            return
        }
        probingHost = host
        hue.discovery.probe(host)
    }

    Component.onCompleted: hue.discovery.start()

    Connections {
        target: hue.discovery
        onProbeFinished: {
            if (host !== page.probingHost)
                return
            page.probingHost = ""
            if (ok)
                page.openPairing(page.bridgeAt(host))
            else
                manualError.text = error
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: hue.discovery.bridges

        PullDownMenu {
            busy: hue.discovery.running
            MenuItem {
                text: qsTr("Search again")
                onClicked: hue.discovery.start()
            }
        }

        header: Column {
            width: list.width

            PageHeader {
                title: qsTr("Find your bridge")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("Connect your phone to the same network as your Hue bridge.")
            }
            SectionHeader {
                text: qsTr("Bridges found")
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeMedium
            enabled: modelData.supported
            onClicked: page.openPairing(modelData)

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: modelData.name
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    opacity: item.enabled ? 1.0 : Theme.opacityLow
                }
                Label {
                    width: parent.width
                    text: modelData.supported ? modelData.host
                                              : qsTr("%1 · this bridge is too old").arg(modelData.host)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }
        }

        footer: Column {
            width: list.width

            Item {
                width: parent.width
                height: Theme.itemSizeMedium
                visible: list.count === 0

                BusyIndicator {
                    anchors.centerIn: parent
                    size: BusyIndicatorSize.Medium
                    running: hue.discovery.running
                }
                Label {
                    anchors.centerIn: parent
                    visible: !hue.discovery.running
                    text: qsTr("No bridges found")
                    color: Theme.secondaryHighlightColor
                }
            }

            SectionHeader {
                text: qsTr("Or enter its address")
            }
            TextField {
                id: address
                width: parent.width
                label: qsTr("IP address")
                placeholderText: "192.168.1.2"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhPreferNumbers
                enabled: page.probingHost === ""
                EnterKey.enabled: text.trim().length > 0
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: page.useAddress(text.trim())
            }
            Label {
                id: manualError
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: text !== ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.errorColor
            }
        }

        VerticalScrollDecorator {}
    }
}
