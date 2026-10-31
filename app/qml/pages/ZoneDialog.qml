import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components"

// Creates a zone (zoneId empty) or edits one: a name, a kind for its icon, and lights from any room.
Dialog {
    id: dialog

    property string zoneId
    property string zoneName
    property string zoneArchetype: "other"

    readonly property bool creating: zoneId === ""
    // Light ids (never fixtures) -> true.
    property var selected: ({})
    property int selectedCount: 0

    function isSelected(id, selection) {
        var lights = hue.resolveLights(id)
        for (var i = 0; i < lights.length; ++i) {
            if (!selection[lights[i]])
                return false
        }
        return lights.length > 0
    }

    function setSelected(id, on) {
        var lights = hue.resolveLights(id)
        var copy = {}
        for (var key in selected)
            copy[key] = selected[key]
        for (var i = 0; i < lights.length; ++i) {
            if (on)
                copy[lights[i]] = true
            else
                delete copy[lights[i]]
        }
        selected = copy
        selectedCount = Object.keys(copy).length
    }

    Component.onCompleted: {
        var lights = creating ? [] : hue.lightsOf(zoneId)
        for (var i = 0; i < lights.length; ++i)
            setSelected(lights[i], true)
    }

    canAccept: nameField.text.trim().length > 0 && selectedCount > 0
    onAccepted: {
        var lights = Object.keys(selected)
        var archetype = kindBox.selected
        if (creating)
            hue.createZone(nameField.text, archetype, lights)
        else
            hue.updateZone(zoneId, nameField.text, archetype, lights)
    }

    RemorsePopup {
        id: remorse
    }

    LightModel {
        id: allLights
        store: hue.store
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: !dialog.creating
            MenuItem {
                text: qsTr("Delete zone")
                onClicked: remorse.execute(qsTr("Deleting zone"), function() {
                    hue.deleteZone(dialog.zoneId)
                    pageStack.replaceAbove(null, Qt.resolvedUrl("RoomsPage.qml"))
                })
            }
        }

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: dialog.creating ? qsTr("New zone") : dialog.zoneName
                acceptText: qsTr("Save")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.creating
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("A zone switches lights from different rooms together, like a room.")
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Name")
                placeholderText: qsTr("Name")
                text: dialog.zoneName
                maximumLength: 32
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            KindComboBox {
                id: kindBox
                archetype: dialog.zoneArchetype
            }

            SectionHeader {
                text: qsTr("Lights")
            }

            Repeater {
                model: allLights
                delegate: TextSwitch {
                    text: model.name
                    description: model.lightCount > 1 ? qsTr("%n spot(s)", "", model.lightCount) : ""
                    checked: dialog.isSelected(model.resourceId, dialog.selected)
                    automaticCheck: false
                    onClicked: dialog.setSelected(model.resourceId, !checked)
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
