import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components/icons.js" as Icons

CoverBackground {
    id: cover

    // Two buttons at most; which of them there are is set in Settings.
    readonly property var actions: hue.coverActions
    readonly property bool hasFirst: actions.length > 0 && actions[0].kind !== "none"
    readonly property bool hasSecond: actions.length > 1 && actions[1].kind !== "none"
    readonly property bool active: hue.state === HueClient.Connected

    RoomModel {
        id: rooms
        store: hue.store
    }

    CoverPlaceholder {
        visible: !hue.configured || rooms.count === 0
        text: hue.configured ? qsTr("Connecting") : qsTr("No bridge")
        icon.source: "/usr/share/icons/hicolor/86x86/apps/harbour-lampje.png"
    }

    Column {
        visible: hue.configured && rooms.count > 0
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            left: parent.left
            right: parent.right
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingSmall

        Label {
            text: "Lampje"
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
        }

        Repeater {
            model: rooms
            delegate: Row {
                visible: index < 5 && model.kind === "room"
                width: parent.width
                spacing: Theme.paddingSmall

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.paddingMedium
                    height: width
                    radius: width / 2
                    color: model.on ? Theme.highlightColor : "transparent"
                    border.width: 1
                    border.color: model.on ? Theme.highlightColor : Theme.secondaryColor
                }
                Label {
                    width: parent.width - Theme.paddingMedium - parent.spacing
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: model.on ? Theme.primaryColor : Theme.secondaryColor
                }
            }
        }
    }

    CoverActionList {
        enabled: cover.active && cover.hasFirst && cover.hasSecond
        CoverAction {
            iconSource: cover.hasFirst ? Icons.coverAction(cover.actions[0]) : ""
            onTriggered: hue.runCoverAction(0)
        }
        CoverAction {
            iconSource: cover.hasSecond ? Icons.coverAction(cover.actions[1]) : ""
            onTriggered: hue.runCoverAction(1)
        }
    }

    CoverActionList {
        enabled: cover.active && cover.hasFirst && !cover.hasSecond
        CoverAction {
            iconSource: cover.hasFirst ? Icons.coverAction(cover.actions[0]) : ""
            onTriggered: hue.runCoverAction(0)
        }
    }

    CoverActionList {
        enabled: cover.active && !cover.hasFirst && cover.hasSecond
        CoverAction {
            iconSource: cover.hasSecond ? Icons.coverAction(cover.actions[1]) : ""
            onTriggered: hue.runCoverAction(1)
        }
    }
}
