import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0

Page {
    id: page

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
        return Format.formatDate(time, Formatter.DateMedium)
    }

    Timer {
        interval: 30 * 1000
        repeat: true
        running: page.status === PageStatus.Active && Qt.application.active
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

    SilicaListView {
        id: list
        anchors.fill: parent
        model: sensors

        header: PageHeader {
            title: qsTr("Sensors and switches")
        }

        delegate: ListItem {
            id: item
            contentHeight: Math.max(column.height + 2 * Theme.paddingMedium, Theme.itemSizeMedium)

            Column {
                id: column
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: toggle.visible ? toggle.left : parent.right
                    rightMargin: toggle.visible ? 0 : Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                Label {
                    width: parent.width
                    visible: model.kind === "motion"
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: model.motion && model.motionEnabled ? Theme.highlightColor : Theme.secondaryColor
                    text: {
                        if (!model.motionEnabled)
                            return qsTr("Switched off")
                        var when = page.ago(model.motionChanged)
                        var state = model.motion ? qsTr("Motion") : qsTr("No motion")
                        return when ? state + " · " + when : state
                    }
                }

                Label {
                    width: parent.width
                    visible: text !== ""
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: {
                        var parts = []
                        if (model.temperature !== undefined)
                            parts.push(qsTr("%1 °C").arg(model.temperature.toLocaleString(Qt.locale(), "f", 1)))
                        if (model.lightLevel !== undefined)
                            parts.push(qsTr("%1 lux").arg(Math.round(model.lightLevel)))
                        return parts.join(" · ")
                    }
                }

                Label {
                    width: parent.width
                    visible: model.lastButton > 0
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: qsTr("Button %1 %2").arg(model.lastButton).arg(page.buttonEvent(model.lastButtonEvent))
                          + " · " + page.ago(model.lastButtonTime)
                }

                Label {
                    width: parent.width
                    visible: !model.reachable || model.batteryLevel >= 0
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: !model.reachable || model.batteryLow ? Theme.errorColor : Theme.secondaryColor
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

                Label {
                    width: parent.width
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    opacity: Theme.opacityHigh
                    text: model.productName
                }
            }

            Switch {
                id: toggle
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                    verticalCenter: parent.verticalCenter
                }
                // Only the bridge's sensors can be switched off; zigbee2mqtt's ("z2m:...") can't.
                visible: model.motionId !== "" && model.motionId.indexOf(":") < 0
                checked: model.motionEnabled
                automaticCheck: false
                onClicked: hue.setEnabled(model.motionId, !model.motionEnabled)
            }
        }

        ViewPlaceholder {
            enabled: sensors.count === 0
            text: qsTr("No sensors or switches")
        }

        VerticalScrollDecorator {}
    }
}
