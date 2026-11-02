import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components/schedules.js" as Schedules

// The bridge's automations: wake up, go to sleep, timers, ... They're made in the Hue app; here
// they can be switched on or off, moved to another time or day, or deleted.
Page {
    id: page

    function formatTime(hour, minute) {
        return Format.formatDate(new Date(2000, 0, 1, hour, minute), Formatter.TimeValue)
    }

    ScheduleModel {
        id: schedules
        store: hue.store
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: schedules

        header: PageHeader {
            title: qsTr("Schedules")
        }

        delegate: ListItem {
            id: item
            contentHeight: Math.max(column.height + 2 * Theme.paddingMedium, Theme.itemSizeMedium)

            onClicked: pageStack.push(Qt.resolvedUrl("ScheduleDialog.qml"), {
                                          scheduleId: model.resourceId,
                                          name: model.name,
                                          kindName: Schedules.kindName(model.kind, model.scriptName),
                                          editable: model.editable,
                                          hour: model.hour,
                                          minute: model.minute,
                                          days: model.days
                                      })

            Column {
                id: column
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: toggle.left
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    opacity: model.enabled ? 1.0 : Theme.opacityHigh
                }

                Label {
                    width: parent.width
                    visible: text !== ""
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: model.enabled ? Theme.highlightColor : Theme.secondaryColor
                    text: Schedules.whenText(model, Qt.locale(), page.formatTime)
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: {
                        var parts = [Schedules.kindName(model.kind, model.scriptName)]
                        if (model.places.length > 0)
                            parts.push(model.places.join(", "))
                        return parts.join(" · ")
                    }
                }

                Label {
                    width: parent.width
                    visible: model.error !== ""
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.errorColor
                    text: qsTr("Not working: %1").arg(model.error)
                }
            }

            Switch {
                id: toggle
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                    verticalCenter: parent.verticalCenter
                }
                checked: model.enabled
                automaticCheck: false
                enabled: hue.state === HueClient.Connected
                onClicked: hue.setEnabled(model.resourceId, !model.enabled)
            }
        }

        ViewPlaceholder {
            enabled: schedules.count === 0
            text: qsTr("No schedules")
            hintText: qsTr("Wake up, go to sleep and other automations are set up in the Hue app")
        }

        VerticalScrollDecorator {}
    }
}
