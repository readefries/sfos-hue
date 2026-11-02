import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0

Kirigami.ScrollablePage {
    id: page

    title: qsTr("Settings")

    function stateText(state) {
        switch (state) {
        case HueClient.Connected: return qsTr("Connected")
        case HueClient.Connecting: return qsTr("Connecting")
        case HueClient.Reconnecting: return qsTr("Waiting to reconnect")
        case HueClient.Unauthorized: return qsTr("Not paired")
        case HueClient.Untrusted: return qsTr("Certificate changed")
        default: return ""
        }
    }

    function mqttStateText(state) {
        switch (state) {
        case HueClient.MqttConnected: return qsTr("Connected")
        case HueClient.MqttConnecting: return qsTr("Connecting")
        case HueClient.MqttReconnecting: return qsTr("Waiting to reconnect")
        case HueClient.MqttRefused: return qsTr("Refused")
        default: return ""
        }
    }

    Kirigami.PromptDialog {
        id: confirmForget
        title: qsTr("Forget bridge")
        subtitle: qsTr("Forgetting only removes the bridge from this computer. To revoke the app's access, remove \"%1\" in the Hue app.").arg(hue.deviceType)
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onAccepted: hue.forget()
    }

    Kirigami.PromptDialog {
        id: confirmRemoveBroker
        title: qsTr("Remove")
        subtitle: hue.mqttHost
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onAccepted: hue.forgetMqtt()
    }

    Kirigami.FormLayout {
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Bridge")
        }
        QQC2.Label {
            Kirigami.FormData.label: qsTr("Name")
            text: hue.bridgeName
        }
        QQC2.Label {
            Kirigami.FormData.label: qsTr("Address")
            text: hue.bridgeHost
        }
        QQC2.Label {
            Kirigami.FormData.label: qsTr("Status")
            text: page.stateText(hue.state)
        }
        QQC2.Label {
            visible: hue.lastError !== "" && hue.state !== HueClient.Connected
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.Wrap
            color: Kirigami.Theme.negativeTextColor
            text: hue.lastError
        }
        RowLayout {
            QQC2.Button {
                text: qsTr("Reconnect")
                icon.name: "view-refresh"
                onClicked: hue.reconnect()
            }
            QQC2.Button {
                text: qsTr("Forget bridge")
                icon.name: "edit-delete"
                onClicked: confirmForget.open()
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Zigbee (MQTT)")
        }
        QQC2.Label {
            visible: !hue.mqttConfigured
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: qsTr("Lights and sensors from zigbee2mqtt, through an MQTT broker")
        }
        QQC2.Label {
            Kirigami.FormData.label: qsTr("Broker")
            visible: hue.mqttConfigured
            text: hue.mqttHost + ":" + hue.mqttPort
        }
        QQC2.Label {
            Kirigami.FormData.label: qsTr("Status")
            visible: hue.mqttConfigured
            text: page.mqttStateText(hue.mqttState)
        }
        QQC2.Label {
            visible: hue.mqttError !== "" && hue.mqttState !== HueClient.MqttConnected
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.Wrap
            color: Kirigami.Theme.negativeTextColor
            text: hue.mqttError
        }
        RowLayout {
            QQC2.Button {
                text: hue.mqttConfigured ? qsTr("Change") : qsTr("Set up")
                icon.name: "configure"
                onClicked: applicationWindow().openForm(Qt.resolvedUrl("MqttDialog.qml"))
            }
            QQC2.Button {
                visible: hue.mqttConfigured
                text: qsTr("Remove")
                icon.name: "edit-delete"
                onClicked: confirmRemoveBroker.open()
            }
        }
    }
}
