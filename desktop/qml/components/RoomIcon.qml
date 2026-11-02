import QtQuick
import org.kde.kirigami as Kirigami
import "icons.js" as Icons

// The kind of room or zone, from its archetype; highlighted while any of its lights are on.
Kirigami.Icon {
    property string archetype
    property bool on: true

    implicitWidth: Kirigami.Units.iconSizes.medium
    implicitHeight: implicitWidth
    source: Qt.resolvedUrl(Icons.room(archetype))
    isMask: true
    color: on ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
}
