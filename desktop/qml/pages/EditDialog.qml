import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import "../components"

// A name, and a kind for rooms; the caller saves them from onAccepted.
FormPage {
    id: form

    property string name
    property string archetype
    property bool withKind: false

    readonly property string newName: nameField.text.trim()
    readonly property string newArchetype: withKind ? kindBox.selected : ""

    canAccept: newName.length > 0

    Kirigami.FormLayout {
        QQC2.TextField {
            id: nameField
            Kirigami.FormData.label: qsTr("Name")
            text: form.name
            maximumLength: 32
            focus: true
            onAccepted: form.accept()
        }
        KindComboBox {
            id: kindBox
            Kirigami.FormData.label: qsTr("Kind")
            visible: form.withKind
            archetype: form.archetype
        }
    }
}
