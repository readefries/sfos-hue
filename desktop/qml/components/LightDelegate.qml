import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One light: its colour, on/off and brightness; clicked opens colour and temperature.
QQC2.ItemDelegate {
    id: item

    width: ListView.view ? ListView.view.width : (parent ? parent.width : implicitWidth)

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            spacing: Kirigami.Units.largeSpacing

            LightIcon {
                archetype: model.archetype
                lightColor: model.displayColor
                on: model.on && model.reachable
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
                    visible: !model.reachable || model.lightCount > 1
                    text: !model.reachable ? qsTr("Unreachable") : qsTr("%n spot(s)", "", model.lightCount)
                    font: Kirigami.Theme.smallFont
                    color: !model.reachable ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                }
            }

            QQC2.Switch {
                checked: model.on
                onToggled: {
                    checked = Qt.binding(function() { return model.on })
                    hue.setOn(model.resourceId, !model.on)
                }
            }
        }

        LevelSlider {
            Layout.fillWidth: true
            visible: model.supportsDimming
            opacity: model.on ? 1.0 : 0.6
            from: 1
            to: 100

            level: model.brightness
            onChangeRequested: (value) => hue.setBrightness(model.resourceId, value)
        }
    }
}
