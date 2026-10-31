import QtQuick 2.0
import Sailfish.Silica 1.0

// A room or zone: on/off and brightness, tap for its lights and scenes.
ListItem {
    id: item

    contentHeight: column.height
    onClicked: pageStack.push(Qt.resolvedUrl("../pages/RoomPage.qml"), {
                                  groupId: model.resourceId,
                                  groupName: model.name,
                                  groupKind: model.kind
                              })

    Column {
        id: column
        width: parent.width

        Item {
            width: parent.width
            height: Theme.itemSizeSmall

            RoomIcon {
                id: icon
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                archetype: model.archetype
                on: model.on
            }

            Column {
                anchors {
                    left: icon.right
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
                    text: qsTr("%n light(s)", "", model.lightCount)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            Switch {
                id: toggle
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                    verticalCenter: parent.verticalCenter
                }
                enabled: model.groupedLightId !== ""
                checked: model.on
                automaticCheck: false
                onClicked: hue.setOn(model.groupedLightId, !model.on)
            }
        }

        LevelSlider {
            visible: model.groupedLightId !== ""
            opacity: model.on ? 1.0 : Theme.opacityHigh
            minimumValue: 1
            maximumValue: 100
            stepSize: 1
            level: model.brightness
            onChangeRequested: hue.setBrightness(model.groupedLightId, value)
        }
    }
}
