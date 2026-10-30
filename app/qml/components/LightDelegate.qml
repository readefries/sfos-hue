import QtQuick 2.0
import Sailfish.Silica 1.0

// One light: its colour, on/off and brightness; tap for colour and temperature.
ListItem {
    id: item

    contentHeight: column.height
    onClicked: pageStack.push(Qt.resolvedUrl("../pages/LightPage.qml"), {
                                  lightId: model.resourceId,
                                  lightName: model.name
                              })

    Column {
        id: column
        width: parent.width

        Item {
            width: parent.width
            height: Theme.itemSizeSmall

            Rectangle {
                id: swatch
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                width: Theme.iconSizeSmall
                height: width
                radius: width / 2
                color: model.displayColor
                opacity: model.on && model.reachable ? 1.0 : 0.2
                border.width: model.on ? 0 : 1
                border.color: Theme.secondaryColor
            }

            Column {
                anchors {
                    left: swatch.right
                    leftMargin: Theme.paddingLarge
                    right: toggle.left
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    visible: !model.reachable || model.lightCount > 1
                    text: !model.reachable ? qsTr("Unreachable") : qsTr("%1 spots").arg(model.lightCount)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: !model.reachable ? Theme.errorColor
                                            : item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            Switch {
                id: toggle
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                    verticalCenter: parent.verticalCenter
                }
                checked: model.on
                automaticCheck: false
                onClicked: hue.setOn(model.resourceId, !model.on)
            }
        }

        LevelSlider {
            visible: model.supportsDimming
            opacity: model.on ? 1.0 : Theme.opacityHigh
            minimumValue: 1
            maximumValue: 100
            stepSize: 1
            level: model.brightness
            onChangeRequested: hue.setBrightness(model.resourceId, value)
        }
    }
}
