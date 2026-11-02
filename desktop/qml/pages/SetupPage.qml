import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0

Kirigami.ScrollablePage {
    id: page

    title: qsTr("Find your bridge")

    property string probingHost
    // Why the address typed in didn't work (the footer's ids aren't visible from here).
    property string manualError

    function bridgeAt(host) {
        var bridges = hue.discovery.bridges
        for (var i = 0; i < bridges.length; ++i) {
            if (bridges[i].host === host)
                return bridges[i]
        }
        return null
    }

    function openPairing(bridge) {
        applicationWindow().openAfter(page, Qt.resolvedUrl("PairPage.qml"), {
                                          host: bridge.host,
                                          bridgeId: bridge.id,
                                          bridgeName: bridge.name
                                      })
    }

    function useAddress(host) {
        manualError = ""
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
        function onProbeFinished(host, ok, error) {
            if (host !== page.probingHost)
                return
            page.probingHost = ""
            if (ok)
                page.openPairing(page.bridgeAt(host))
            else
                page.manualError = error
        }
    }

    actions: [
        Kirigami.Action {
            text: qsTr("Search again")
            icon.name: "view-refresh"
            enabled: !hue.discovery.running
            onTriggered: hue.discovery.start()
        }
    ]

    ListView {
        id: list
        model: hue.discovery.bridges

        header: ColumnLayout {
            width: list.width
            spacing: 0
            QQC2.Label {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                wrapMode: Text.Wrap
                text: qsTr("Connect this computer to the same network as your Hue bridge.")
            }
            Kirigami.ListSectionHeader {
                Layout.fillWidth: true
                text: qsTr("Bridges found")
            }
        }

        delegate: QQC2.ItemDelegate {
            width: list.width
            enabled: modelData.supported
            onClicked: page.openPairing(modelData)
            contentItem: ColumnLayout {
                spacing: 0
                QQC2.Label {
                    Layout.fillWidth: true
                    text: modelData.name
                    elide: Text.ElideRight
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: modelData.supported ? modelData.host
                                              : qsTr("%1 · this bridge is too old").arg(modelData.host)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        footer: ColumnLayout {
            width: list.width
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: list.count === 0 && hue.discovery.running
                running: visible
            }
            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                visible: list.count === 0 && !hue.discovery.running
                text: qsTr("No bridges found")
                color: Kirigami.Theme.disabledTextColor
            }

            Kirigami.ListSectionHeader {
                Layout.fillWidth: true
                text: qsTr("Or enter its address")
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                QQC2.TextField {
                    id: address
                    Layout.fillWidth: true
                    placeholderText: "192.168.1.2"
                    enabled: page.probingHost === ""
                    onAccepted: if (text.trim().length > 0) page.useAddress(text.trim())
                }
                QQC2.Button {
                    text: qsTr("Connect")
                    enabled: address.text.trim().length > 0 && page.probingHost === ""
                    onClicked: page.useAddress(address.text.trim())
                }
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Error
                text: page.manualError
                visible: text !== ""
            }
        }
    }
}
