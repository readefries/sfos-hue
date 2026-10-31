import QtQuick 2.0
import Sailfish.Silica 1.0

// Where zigbee2mqtt's MQTT broker is, and how to log in.
Dialog {
    id: dialog

    canAccept: hostField.text.trim().length > 0 && portField.acceptableInput

    onAccepted: hue.setMqttConfig(hostField.text, parseInt(portField.text, 10), userField.text,
                                  passwordField.text, baseField.text)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: qsTr("MQTT broker")
                acceptText: qsTr("Save")
            }

            TextField {
                id: hostField
                width: parent.width
                label: qsTr("Address")
                placeholderText: qsTr("Address, e.g. 192.168.1.10")
                text: hue.mqttHost
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                focus: !hue.mqttConfigured
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: portField.focus = true
            }
            TextField {
                id: portField
                width: parent.width
                label: qsTr("Port")
                placeholderText: qsTr("Port")
                text: hue.mqttPort
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: userField.focus = true
            }
            TextField {
                id: userField
                width: parent.width
                label: qsTr("User name")
                placeholderText: qsTr("User name (if the broker wants one)")
                text: hue.mqttUser
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true
            }
            PasswordField {
                id: passwordField
                width: parent.width
                // Never shown again; left empty, the saved one stays.
                placeholderText: hue.mqttConfigured && hue.mqttUser !== "" && userField.text.trim() === hue.mqttUser
                                 ? qsTr("Password (unchanged)") : qsTr("Password")
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: baseField.focus = true
            }
            TextField {
                id: baseField
                width: parent.width
                label: qsTr("Base topic")
                placeholderText: qsTr("Base topic")
                text: hue.mqttBaseTopic
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.enabled: dialog.canAccept
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: dialog.accept()
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The base topic is zigbee2mqtt's mqtt.base_topic setting; usually zigbee2mqtt. The password is sent unencrypted, so only use this on your own network.")
            }
        }

        VerticalScrollDecorator {}
    }
}
