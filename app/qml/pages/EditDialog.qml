import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// A name, and a kind for rooms; the caller saves them from onAccepted.
Dialog {
    id: dialog

    property string title
    property string name
    property string archetype
    property bool withKind: false

    readonly property string newName: nameField.text.trim()
    readonly property string newArchetype: withKind ? kindBox.selected : ""

    canAccept: newName.length > 0

    Column {
        width: parent.width

        DialogHeader {
            title: dialog.title
            acceptText: qsTr("Save")
        }

        TextField {
            id: nameField
            width: parent.width
            label: qsTr("Name")
            placeholderText: qsTr("Name")
            text: dialog.name
            maximumLength: 32
            focus: true
            EnterKey.enabled: dialog.canAccept
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }

        KindComboBox {
            id: kindBox
            visible: dialog.withKind
            archetype: dialog.archetype
        }
    }
}
