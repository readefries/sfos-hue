import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components"

Page {
    id: page

    property string lightId
    property string lightName

    LightModel {
        id: light
        store: hue.store
        lightId: page.lightId
    }

    SilicaFlickable {
        id: flickable
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: page.lightName
            }

            Repeater {
                model: light
                delegate: Column {
                    width: column.width
                    spacing: Theme.paddingMedium

                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        visible: !model.reachable
                        wrapMode: Text.Wrap
                        color: Theme.errorColor
                        text: qsTr("The bridge can't reach this light. Is it switched on at the wall?")
                    }

                    TextSwitch {
                        text: qsTr("On")
                        checked: model.on
                        automaticCheck: false
                        onClicked: hue.setOn(model.resourceId, !model.on)
                    }

                    LevelSlider {
                        label: qsTr("Brightness")
                        visible: model.supportsDimming
                        minimumValue: 1
                        maximumValue: 100
                        stepSize: 1
                        valueText: Math.round(value) + " %"
                        level: model.brightness
                        onChangeRequested: hue.setBrightness(model.resourceId, value)
                    }

                    SectionHeader {
                        text: qsTr("White")
                        visible: model.supportsTemperature
                    }

                    LevelSlider {
                        id: temperature
                        label: qsTr("Colour temperature")
                        visible: model.supportsTemperature
                        // Cool on the left, warm on the right, like the Hue app.
                        minimumValue: model.mirekMin
                        maximumValue: model.mirekMax
                        stepSize: 1
                        valueText: Math.round(1000000 / value / 50) * 50 + " K"
                        level: model.mirek
                        onChangeRequested: hue.setMirek(model.resourceId, Math.round(value))
                    }

                    // What the temperatures along the slider look like.
                    Row {
                        x: Theme.horizontalPageMargin
                        visible: model.supportsTemperature
                        Repeater {
                            model: 24
                            Rectangle {
                                width: (column.width - 2 * Theme.horizontalPageMargin) / 24
                                height: Theme.paddingSmall
                                color: hue.mirekColor(temperature.minimumValue
                                                      + (temperature.maximumValue - temperature.minimumValue) * index / 23)
                            }
                        }
                    }

                    SectionHeader {
                        text: qsTr("Colour")
                        visible: model.supportsColor
                    }

                    ColorWheel {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: model.supportsColor
                        width: Math.min(page.width, page.height) - 4 * Theme.horizontalPageMargin
                        height: width
                        currentColor: model.displayColor
                        onColorPicked: hue.setColor(model.resourceId, color)
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
