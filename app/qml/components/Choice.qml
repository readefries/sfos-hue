import QtQuick 2.0
import Sailfish.Silica 1.0

// One option in a list to pick from, with a room icon if it has an archetype.
BackgroundItem {
    id: item

    property alias text: label.text
    property string description
    property string archetype
    property bool selected

    width: parent ? parent.width : 0
    height: Theme.itemSizeSmall + (description !== "" ? Theme.paddingLarge : 0)
    highlighted: down || selected

    RoomIcon {
        id: icon
        visible: item.archetype !== ""
        anchors {
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        width: Theme.iconSizeSmall
        archetype: item.archetype
        on: item.highlighted
    }

    Column {
        anchors {
            left: icon.visible ? icon.right : parent.left
            leftMargin: icon.visible ? Theme.paddingLarge : Theme.horizontalPageMargin
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        Label {
            id: label
            width: parent.width
            truncationMode: TruncationMode.Fade
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            visible: item.description !== ""
            text: item.description
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }
}
