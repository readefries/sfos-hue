import QtQuick
import org.kde.kirigami as Kirigami
import "icons.js" as Icons

// The kind of lamp (from its archetype), in the colour it's showing; dimmed when off.
Kirigami.Icon {
    property string archetype
    property color lightColor: "white"
    property bool on: true

    implicitWidth: Kirigami.Units.iconSizes.medium
    implicitHeight: implicitWidth
    source: Qt.resolvedUrl(Icons.light(archetype))
    isMask: true
    // A white lamp on a light theme would vanish; it gets the text colour then.
    color: !on ? Kirigami.Theme.disabledTextColor
               : Kirigami.ColorUtils.brightnessForColor(Kirigami.Theme.backgroundColor) === Kirigami.ColorUtils.Light
                 && Kirigami.ColorUtils.brightnessForColor(lightColor) === Kirigami.ColorUtils.Light
                 && lightColor.hslSaturation < 0.3
                 ? Kirigami.Theme.textColor : lightColor
    opacity: on ? 1.0 : 0.6
}
