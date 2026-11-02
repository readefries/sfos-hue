import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components"
import "../components/schedules.js" as Schedules

// A schedule's time and days, for those that start at a time of day; the rest of it (what it
// does, where, how it fades) stays as the Hue app set it up.
FormPage {
    id: form

    property string scheduleId
    property string name
    property string kindName
    property bool editable
    property int hour
    property int minute
    property var days: []

    // A plain array (days may be the model's string list), changed as a whole so bindings follow.
    property var chosenDays: {
        var copy = []
        for (var i = 0; i < days.length; ++i)
            copy.push(days[i])
        return copy
    }

    function toggleDay(key) {
        var copy = chosenDays.slice()
        var i = copy.indexOf(key)
        if (i >= 0)
            copy.splice(i, 1)
        else
            copy.push(key)
        chosenDays = copy
    }

    title: name
    canAccept: editable && hue.state === HueClient.Connected
    deleteText: qsTr("Delete")
    deleteQuestion: name

    onAccepted: hue.setScheduleTime(scheduleId, hourBox.value, minuteBox.value, chosenDays)
    onDeleteConfirmed: hue.deleteSchedule(scheduleId)

    Kirigami.FormLayout {
        QQC2.Label {
            Kirigami.FormData.label: qsTr("Kind")
            text: form.kindName
        }

        RowLayout {
            Kirigami.FormData.label: qsTr("Time")
            visible: form.editable
            QQC2.SpinBox {
                id: hourBox
                from: 0
                to: 23
                value: form.hour
                editable: true
                textFromValue: function(value) { return (value < 10 ? "0" : "") + value }
            }
            QQC2.Label {
                text: ":"
            }
            QQC2.SpinBox {
                id: minuteBox
                from: 0
                to: 59
                value: form.minute
                editable: true
                wrap: true
                textFromValue: function(value) { return (value < 10 ? "0" : "") + value }
            }
        }

        Repeater {
            model: form.editable ? Schedules.daysInLocaleOrder(Qt.locale()) : []
            delegate: QQC2.CheckBox {
                Kirigami.FormData.label: index === 0 ? qsTr("Days") : ""
                text: modelData.name
                checked: form.chosenDays.indexOf(modelData.key) >= 0
                onToggled: form.toggleDay(modelData.key)
            }
        }

        QQC2.Label {
            visible: form.editable && form.chosenDays.length === 0
            color: Kirigami.Theme.disabledTextColor
            text: qsTr("Without days, it runs once.")
        }

        QQC2.Label {
            visible: !form.editable
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: qsTr("This schedule doesn't start at a fixed time; change it in the Hue app.")
        }
    }
}
