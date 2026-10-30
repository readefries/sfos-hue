import QtQuick 2.0
import Sailfish.Silica 1.0

// A slider that follows `level` from the model, except while the finger is on it, and asks for
// changes as it's dragged. Commands are coalesced further down, so asking often is fine.
Slider {
    id: slider

    property real level
    signal changeRequested(real value)

    width: parent ? parent.width : 0
    Component.onCompleted: value = level
    onLevelChanged: if (!down) value = level
    onValueChanged: if (down) changeRequested(value)
    onDownChanged: if (!down) changeRequested(value)
}
