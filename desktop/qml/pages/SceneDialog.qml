import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../components"

// Creates a scene from a room's lights as they are now (sceneId empty), or edits one.
FormPage {
    id: form

    property string groupId
    property string groupName
    property string sceneId
    property string sceneName

    readonly property bool creating: sceneId === ""

    title: creating ? qsTr("New scene") : sceneName
    canAccept: nameField.text.trim().length > 0
    deleteText: creating ? "" : qsTr("Delete scene")
    deleteQuestion: sceneName

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
    onDeleteConfirmed: hue.deleteScene(sceneId)

    Kirigami.FormLayout {
        QQC2.TextField {
            id: nameField
            Kirigami.FormData.label: qsTr("Name")
            text: form.sceneName
            maximumLength: 32
            focus: true
            onAccepted: form.accept()
        }
        QQC2.CheckBox {
            id: storeSwitch
            visible: !form.creating
            text: qsTr("Use the lights as they are now")
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: form.creating
                  ? qsTr("The scene remembers the lights in %1 as they are now: which are on, how bright, and their colour.").arg(form.groupName)
                  : qsTr("Replaces what the scene does with the current state of the lights in %1").arg(form.groupName)
        }
    }
}
