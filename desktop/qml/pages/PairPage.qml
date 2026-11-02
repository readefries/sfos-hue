import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0

Kirigami.Page {
    id: page

    property string host
    property string bridgeId
    property string bridgeName

    readonly property bool failed: hue.pairingState === HueClient.PairingFailed

    title: bridgeName

    Component.onCompleted: hue.pair(host, bridgeId, bridgeName)
    Component.onDestruction: hue.cancelPairing()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width, Kirigami.Units.gridUnit * 20)
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: page.host
            color: Kirigami.Theme.disabledTextColor
        }

        Kirigami.Heading {
            Layout.alignment: Qt.AlignHCenter
            visible: !page.failed
            level: 1
            text: hue.pairingSecondsLeft
        }
        QQC2.ProgressBar {
            Layout.fillWidth: true
            visible: !page.failed
            from: 0
            to: 60
            value: hue.pairingSecondsLeft
        }
        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            visible: page.failed
            source: "dialog-warning"
            implicitWidth: Kirigami.Units.iconSizes.huge
            implicitHeight: implicitWidth
        }

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
            text: page.failed ? hue.pairingError
                              : qsTr("Press the round button on top of the bridge")
        }

        QQC2.Button {
            Layout.alignment: Qt.AlignHCenter
            visible: page.failed
            text: qsTr("Try again")
            onClicked: hue.pair(page.host, page.bridgeId, page.bridgeName)
        }
    }
}
