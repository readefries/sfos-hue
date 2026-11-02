import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A room or zone: on/off and brightness; clicked opens its lights and scenes (the page decides).
QQC2.ItemDelegate {
    id: item

    width: ListView.view ? ListView.view.width : implicitWidth

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            spacing: Kirigami.Units.largeSpacing

            RoomIcon {
                archetype: model.archetype
                on: model.on
            }

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
                    text: qsTr("%n light(s)", "", model.lightCount)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }

            QQC2.Switch {
                enabled: model.lightCount > 0
                checked: model.on
                // The model says what it is; a click asks for the other.
                onToggled: {
                    checked = Qt.binding(function() { return model.on })
                    hue.setOn(model.resourceId, !model.on)
                }
            }
        }

        LevelSlider {
            Layout.fillWidth: true
            visible: model.dimmable
            opacity: model.on ? 1.0 : 0.6
            from: 1
            to: 100

            level: model.brightness
            onChangeRequested: (value) => hue.setBrightness(model.resourceId, value)
        }
    }
}
