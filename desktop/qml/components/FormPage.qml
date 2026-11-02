import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0

// A form over the window (a layer): Save, Cancel and, if deleteText is set, Delete after asking.
// What it edits is saved from onAccepted / onDeleteConfirmed; the form closes itself.
Kirigami.ScrollablePage {
    id: form

    property bool canAccept: true
    property string acceptText: qsTr("Save")
    property string deleteText
    property string deleteQuestion
    signal accepted()
    signal deleteConfirmed()

    function accept() {
        if (!canAccept)
            return
        accepted()
        applicationWindow().closeForm()
    }

    actions: [
        Kirigami.Action {
            text: form.acceptText
            icon.name: "dialog-ok-apply"
            enabled: form.canAccept
            shortcut: "Ctrl+Return"
            onTriggered: form.accept()
        },
        Kirigami.Action {
            text: form.deleteText
            icon.name: "edit-delete"
            visible: form.deleteText !== ""
            enabled: hue.state === HueClient.Connected
            onTriggered: confirm.open()
        },
        Kirigami.Action {
            text: qsTr("Cancel")
            icon.name: "dialog-cancel"
            shortcut: "Escape"
            onTriggered: applicationWindow().closeForm()
        }
    ]

    Kirigami.PromptDialog {
        id: confirm
        title: form.deleteText
        subtitle: form.deleteQuestion
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onAccepted: {
            form.deleteConfirmed()
            applicationWindow().closeForm()
        }
    }
}
