import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components/schedules.js" as Schedules

// A schedule's time and days, for those that start at a time of day; the rest of it (what it
// does, where, how it fades) stays as the Hue app set it up. Pull down to delete.
Dialog {
    id: dialog

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

    canAccept: editable && hue.state === HueClient.Connected

    onAccepted: hue.setScheduleTime(scheduleId, hour, minute, chosenDays)

    function toggleDay(key) {
        var copy = chosenDays.slice()
        var i = copy.indexOf(key)
        if (i >= 0)
            copy.splice(i, 1)
        else
            copy.push(key)
        chosenDays = copy
    }

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Delete")
                enabled: hue.state === HueClient.Connected
                onClicked: remorse.execute(qsTr("Deleting schedule"), function() {
                    hue.deleteSchedule(dialog.scheduleId)
                    pageStack.pop()
                })
            }
        }

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: dialog.name
                acceptText: qsTr("Save")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: dialog.kindName
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            ValueButton {
                visible: dialog.editable
                label: qsTr("Time")
                value: Format.formatDate(new Date(2000, 0, 1, dialog.hour, dialog.minute), Formatter.TimeValue)
                onClicked: {
                    var picker = pageStack.push("Sailfish.Silica.TimePickerDialog", {
                                                    hour: dialog.hour,
                                                    minute: dialog.minute
                                                })
                    picker.accepted.connect(function() {
                        dialog.hour = picker.hour
                        dialog.minute = picker.minute
                    })
                }
            }

            SectionHeader {
                visible: dialog.editable
                text: qsTr("Days")
            }

            Repeater {
                model: dialog.editable ? Schedules.daysInLocaleOrder(Qt.locale()) : []
                delegate: TextSwitch {
                    text: modelData.name
                    checked: dialog.chosenDays.indexOf(modelData.key) >= 0
                    automaticCheck: false
                    onClicked: dialog.toggleDay(modelData.key)
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.editable
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: dialog.chosenDays.length === 0 ? qsTr("Without days, it runs once.") : ""
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: !dialog.editable
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("This schedule doesn't start at a fixed time; change it in the Hue app.")
            }
        }
    }
}
