import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0

ApplicationWindow {
    initialPage: hue.configured ? Qt.resolvedUrl("pages/RoomsPage.qml")
                                : Qt.resolvedUrl("pages/SetupPage.qml")
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    Connections {
        target: hue
        onPaired: pageStack.replaceAbove(null, Qt.resolvedUrl("pages/RoomsPage.qml"))
        onConfiguredChanged: {
            if (!hue.configured)
                pageStack.replaceAbove(null, Qt.resolvedUrl("pages/SetupPage.qml"))
        }
    }
}
