import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components/schedules.js" as Schedules

// The bridge's automations: wake up, go to sleep, timers, ... They're made in the Hue app; here
// they can be switched on or off, moved to another time or day, or deleted.
Kirigami.ScrollablePage {
    id: page

    title: qsTr("Schedules")

    // The desktop's 24h/12h clock setting if it has one (en_US with a 24h clock is common),
    // otherwise the locale's way.
    function formatTime(hour, minute) {
        var time = new Date(2000, 0, 1, hour, minute)
        if (desktop.clockFormat === "24h")
            return time.toLocaleTimeString(Qt.locale(), "HH:mm")
        if (desktop.clockFormat === "12h")
            return time.toLocaleTimeString(Qt.locale(), "h:mm AP")
        return time.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
    }

    ScheduleModel {
        id: schedules
        store: hue.store
    }

    ListView {
        id: list
        model: schedules

        delegate: QQC2.ItemDelegate {
            id: item
            width: list.width

            onClicked: applicationWindow().openForm(Qt.resolvedUrl("ScheduleDialog.qml"), {
                                                        scheduleId: model.resourceId,
                                                        name: model.name,
                                                        kindName: Schedules.kindName(model.kind, model.scriptName),
                                                        editable: model.editable,
                                                        hour: model.hour,
                                                        minute: model.minute,
                                                        days: model.days
                                                    })

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: model.name
                        elide: Text.ElideRight
                        opacity: model.enabled ? 1.0 : 0.6
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        wrapMode: Text.Wrap
                        color: model.enabled ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                        text: Schedules.whenText(model, Qt.locale(), page.formatTime)
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        text: {
                            var parts = [Schedules.kindName(model.kind, model.scriptName)]
                            if (model.places.length > 0)
                                parts.push(model.places.join(", "))
                            return parts.join(" · ")
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: model.error !== ""
                        wrapMode: Text.Wrap
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.negativeTextColor
                        text: qsTr("Not working: %1").arg(model.error)
                    }
                }

                QQC2.Switch {
                    checked: model.enabled
                    enabled: hue.state === HueClient.Connected
                    onToggled: {
                        checked = Qt.binding(function() { return model.enabled })
                        hue.setEnabled(model.resourceId, !model.enabled)
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: schedules.count === 0
            text: qsTr("No schedules")
            explanation: qsTr("Wake up, go to sleep and other automations are set up in the Hue app")
        }
    }
}
