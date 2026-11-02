import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../components"

// Where zigbee2mqtt's MQTT broker is, and how to log in.
FormPage {
    id: form

    title: qsTr("MQTT broker")
    canAccept: hostField.text.trim().length > 0 && portField.acceptableInput

    onAccepted: hue.setMqttConfig(hostField.text, parseInt(portField.text, 10), userField.text,
                                  passwordField.text, baseField.text)

    Kirigami.FormLayout {
        QQC2.TextField {
            id: hostField
            Kirigami.FormData.label: qsTr("Address")
            placeholderText: "192.168.1.10"
            text: hue.mqttHost
            focus: true
        }
        QQC2.TextField {
            id: portField
            Kirigami.FormData.label: qsTr("Port")
            text: hue.mqttPort
            validator: IntValidator { bottom: 1; top: 65535 }
        }
        QQC2.TextField {
            id: userField
            Kirigami.FormData.label: qsTr("User name")
            placeholderText: qsTr("User name (if the broker wants one)")
            text: hue.mqttUser
        }
        Kirigami.PasswordField {
            id: passwordField
            Kirigami.FormData.label: qsTr("Password")
            // Never shown again; left empty, the saved one stays.
            placeholderText: hue.mqttConfigured && hue.mqttUser !== "" && userField.text.trim() === hue.mqttUser
                             ? qsTr("Password (unchanged)") : ""
        }
        QQC2.TextField {
            id: baseField
            Kirigami.FormData.label: qsTr("Base topic")
            text: hue.mqttBaseTopic
            onAccepted: form.accept()
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: qsTr("The base topic is zigbee2mqtt's mqtt.base_topic setting; usually zigbee2mqtt. The password is sent unencrypted, so only use this on your own network.")
        }
    }
}
