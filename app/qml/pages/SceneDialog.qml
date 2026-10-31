import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0

// Creates a scene from a room's lights as they are now (sceneId empty), or edits one.
Dialog {
    id: dialog

    property string groupId
    property string groupName
    property string sceneId
    property string sceneName

    readonly property bool creating: sceneId === ""

    canAccept: nameField.text.trim().length > 0
    onAccepted: {
        var name = nameField.text.trim()
        if (creating) {
            hue.createScene(groupId, name)
            return
        }
        if (name !== sceneName)
            hue.renameScene(sceneId, name)
        if (storeSwitch.checked)
            hue.storeScene(sceneId)
    }

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            visible: !dialog.creating
            MenuItem {
                text: qsTr("Delete scene")
                onClicked: remorse.execute(qsTr("Deleting scene"), function() {
                    hue.deleteScene(dialog.sceneId)
                    pageStack.pop()
                })
            }
        }

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: dialog.creating ? qsTr("New scene") : dialog.sceneName
                acceptText: qsTr("Save")
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Name")
                placeholderText: qsTr("Name")
                text: dialog.sceneName
                maximumLength: 32
                focus: dialog.creating
                EnterKey.enabled: dialog.canAccept
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: dialog.accept()
            }

            TextSwitch {
                id: storeSwitch
                visible: !dialog.creating
                text: qsTr("Use the lights as they are now")
                description: qsTr("Replaces what the scene does with the current state of the lights in %1")
                             .arg(dialog.groupName)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.creating
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The scene remembers the lights in %1 as they are now: which are on, how bright, and their colour.")
                      .arg(dialog.groupName)
            }
        }
    }
}
