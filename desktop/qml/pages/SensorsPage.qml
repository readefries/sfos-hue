import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0

Kirigami.ScrollablePage {
    id: page

    title: qsTr("Sensors and switches")

    // "5 min ago" and the like, refreshed by the timer below.
    property date now: new Date()

    function ago(time) {
        if (!time || isNaN(time))
            return ""
        var minutes = Math.floor((now - time) / 60000)
        if (minutes < 1)
            return qsTr("just now")
        if (minutes < 60)
            return qsTr("%n min ago", "", minutes)
        var hours = Math.floor(minutes / 60)
        if (hours < 24)
            return qsTr("%n h ago", "", hours)
        var days = Math.floor(hours / 24)
        if (days < 7)
            return days === 1 ? qsTr("yesterday") : qsTr("%n day(s) ago", "", days)
        return time.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    Timer {
        interval: 30 * 1000
        repeat: true
        running: Qt.application.state === Qt.ApplicationActive
        triggeredOnStart: true
        onTriggered: page.now = new Date()
    }

    function buttonEvent(event) {
        switch (event) {
        case "initial_press": return qsTr("pressed")
        case "short_release": return qsTr("pressed")
        case "long_press": return qsTr("held")
        case "long_release": return qsTr("held")
        case "repeat": return qsTr("held")
        default: return event
        }
    }

    SensorModel {
        id: sensors
        store: hue.store
    }

    ListView {
        id: list
        model: sensors

        delegate: QQC2.ItemDelegate {
            id: item
            width: list.width
            hoverEnabled: false
            down: false

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: model.name
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: model.kind === "motion"
                        wrapMode: Text.Wrap
                        color: model.motion && model.motionEnabled ? Kirigami.Theme.highlightColor
                                                                   : Kirigami.Theme.disabledTextColor
                        text: {
                            if (!model.motionEnabled)
                                return qsTr("Switched off")
                            var when = page.ago(model.motionChanged)
                            var state = model.motion ? qsTr("Motion") : qsTr("No motion")
                            return when ? state + " · " + when : state
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        color: Kirigami.Theme.disabledTextColor
                        text: {
                            var parts = []
                            if (model.temperature !== undefined)
                                parts.push(qsTr("%1 °C").arg(model.temperature.toLocaleString(Qt.locale(), "f", 1)))
                            if (model.lightLevel !== undefined)
                                parts.push(qsTr("%1 lux").arg(Math.round(model.lightLevel)))
                            return parts.join(" · ")
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: model.lastButton > 0
                        wrapMode: Text.Wrap
                        color: Kirigami.Theme.disabledTextColor
                        text: qsTr("Button %1 %2").arg(model.lastButton).arg(page.buttonEvent(model.lastButtonEvent))
                              + " · " + page.ago(model.lastButtonTime)
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: !model.reachable || model.batteryLevel >= 0
                        font: Kirigami.Theme.smallFont
                        color: !model.reachable || model.batteryLow ? Kirigami.Theme.negativeTextColor
                                                                    : Kirigami.Theme.disabledTextColor
                        text: {
                            var parts = []
                            if (!model.reachable)
                                parts.push(qsTr("Unreachable"))
                            if (model.batteryLevel >= 0)
                                parts.push(model.batteryLow ? qsTr("Battery low: %1 %").arg(model.batteryLevel)
                                                            : qsTr("Battery %1 %").arg(model.batteryLevel))
                            return parts.join(" · ")
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        text: model.productName
                    }
                }

                QQC2.Switch {
                    // Only the bridge's sensors can be switched off; zigbee2mqtt's ("z2m:...") can't.
                    visible: model.motionId !== "" && model.motionId.indexOf(":") < 0
                    checked: model.motionEnabled
                    onToggled: {
                        checked = Qt.binding(function() { return model.motionEnabled })
                        hue.setEnabled(model.motionId, !model.motionEnabled)
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            visible: sensors.count === 0
            text: qsTr("No sensors or switches")
        }
    }
}
