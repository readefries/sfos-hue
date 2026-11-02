import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components"

// Creates a zone (zoneId empty) or edits one: a name, a kind for its icon, and lights from any room.
FormPage {
    id: form

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

    title: creating ? qsTr("New zone") : zoneName
    canAccept: nameField.text.trim().length > 0 && selectedCount > 0
    deleteText: creating ? "" : qsTr("Delete zone")
    deleteQuestion: zoneName

    onAccepted: {
        var lights = Object.keys(selected)
        if (creating)
            hue.createZone(nameField.text, kindBox.selected, lights)
        else
            hue.updateZone(zoneId, nameField.text, kindBox.selected, lights)
    }
    onDeleteConfirmed: {
        hue.deleteZone(zoneId)
        var stack = applicationWindow().pageStack
        while (stack.depth > 1)
            stack.pop()
    }

    LightModel {
        id: allLights
        store: hue.store
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            visible: form.creating
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: qsTr("A zone switches lights from different rooms together, like a room.")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: qsTr("Name")
                text: form.zoneName
                maximumLength: 32
                focus: true
            }
            KindComboBox {
                id: kindBox
                Kirigami.FormData.label: qsTr("Kind")
                archetype: form.zoneArchetype
            }
        }

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: qsTr("Lights")
        }
        Repeater {
            model: allLights
            delegate: QQC2.CheckDelegate {
                Layout.fillWidth: true
                text: model.lightCount > 1 ? model.name + " · " + qsTr("%n spot(s)", "", model.lightCount)
                                           : model.name
                checked: form.isSelected(model.resourceId, form.selected)
                onToggled: form.setSelected(model.resourceId, checked)
            }
        }
    }
}
