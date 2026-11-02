import QtQuick
import QtQuick.Controls as QQC2

// A slider that follows `level` from the model, except while it's being dragged, and asks for
// changes as it moves. Commands are coalesced further down, so asking often is fine.
QQC2.Slider {
    id: slider

    property real level
    signal changeRequested(real value)

    // Whole steps, without the tick for each that a stepSize draws.
    stepSize: 0

    Component.onCompleted: value = level
    onLevelChanged: if (!pressed) value = level
    onMoved: changeRequested(Math.round(value))
    onPressedChanged: if (!pressed) changeRequested(Math.round(value))
}
