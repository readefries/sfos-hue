import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components"

Kirigami.ScrollablePage {
    id: page

    property string lightId
    property string lightName

    title: lightName

    LightModel {
        id: light
        store: hue.store
        lightId: page.lightId
    }

    // For a lamp with several spots: each of them.
    LightModel {
        id: spots
        store: hue.store
        lightId: page.lightId
        groupFixtures: false
    }

    // A lamp with several spots ("fixture:...") is named after them; rename the spots instead.
    // Zigbee lights ("z2m:...") are named in zigbee2mqtt, but their room is picked here; the
    // bridge's lights are in a room through the Hue app.
    actions: [
        Kirigami.Action {
            text: qsTr("Room")
            icon.name: "go-home"
            visible: page.lightId.indexOf("fixture:") !== 0 && !hue.isFromBridge(page.lightId)
            onTriggered: applicationWindow().openForm(Qt.resolvedUrl("RoomPickerPage.qml"), {
                                                          lightId: page.lightId,
                                                          lightName: page.lightName
                                                      })
        },
        Kirigami.Action {
            text: qsTr("Rename")
            icon.name: "edit-rename"
            visible: page.lightId.indexOf("fixture:") !== 0 && hue.isFromBridge(page.lightId)
            onTriggered: {
                var form = applicationWindow().openForm(Qt.resolvedUrl("EditDialog.qml"), {
                                                            title: qsTr("Rename light"),
                                                            name: page.lightName
                                                        })
                form.accepted.connect(function() {
                    hue.renameLight(page.lightId, form.newName)
                })
            }
        }
    ]

    ColumnLayout {
        id: column
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: light
            delegate: ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                // Follow renames.
                property string liveName: model.name
                onLiveNameChanged: page.lightName = liveName

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !model.reachable
                    type: Kirigami.MessageType.Warning
                    text: qsTr("The bridge can't reach this light. Is it switched on at the wall?")
                }

                QQC2.Switch {
                    text: qsTr("On")
                    checked: model.on
                    onToggled: {
                        checked = Qt.binding(function() { return model.on })
                        hue.setOn(model.resourceId, !model.on)
                    }
                }

                RowLayout {
                    visible: model.supportsDimming
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: qsTr("Brightness")
                    }
                    QQC2.Label {
                        text: Math.round(brightness.value) + " %"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                LevelSlider {
                    id: brightness
                    Layout.fillWidth: true
                    visible: model.supportsDimming
                    from: 1
                    to: 100
                    level: model.brightness
                    onChangeRequested: (value) => hue.setBrightness(model.resourceId, value)
                }

                Kirigami.ListSectionHeader {
                    Layout.fillWidth: true
                    text: qsTr("White")
                    visible: model.supportsTemperature
                }
                RowLayout {
                    visible: model.supportsTemperature
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: qsTr("Colour temperature")
                    }
                    QQC2.Label {
                        text: Math.round(1000000 / temperature.value / 50) * 50 + " K"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                LevelSlider {
                    id: temperature
                    Layout.fillWidth: true
                    visible: model.supportsTemperature
                    // Cool on the left, warm on the right, like the Hue app.
                    from: model.mirekMin
                    to: model.mirekMax
                    level: model.mirek
                    onChangeRequested: (value) => hue.setMirek(model.resourceId, Math.round(value))
                }
                // What the temperatures along the slider look like.
                Row {
                    Layout.fillWidth: true
                    visible: model.supportsTemperature
                    Repeater {
                        model: 24
                        Rectangle {
                            width: column.width / 24
                            height: Kirigami.Units.smallSpacing
                            color: hue.mirekColor(temperature.from + (temperature.to - temperature.from) * index / 23)
                        }
                    }
                }

                Kirigami.ListSectionHeader {
                    Layout.fillWidth: true
                    text: qsTr("Colour")
                    visible: model.supportsColor
                }
                ColorWheel {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    visible: model.supportsColor
                    Layout.preferredWidth: Math.min(column.width, Kirigami.Units.gridUnit * 18)
                    Layout.preferredHeight: Layout.preferredWidth
                    currentColor: model.displayColor
                    onColorPicked: (color) => hue.setColor(model.resourceId, color)
                }
            }
        }

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: qsTr("Spots")
            visible: spots.count > 1
        }
        Repeater {
            model: spots.count > 1 ? spots : null
            delegate: LightDelegate {
                Layout.fillWidth: true
                onClicked: applicationWindow().openAfter(page, Qt.resolvedUrl("LightPage.qml"), {
                                                             lightId: model.resourceId,
                                                             lightName: model.name
                                                         })
            }
        }
    }
}
