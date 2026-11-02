import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0

// Rooms on the left; a room, light and so on open in columns next to it as far as the window is
// wide. Forms (renaming, zones, scenes, ...) open as layers over everything.
Kirigami.ApplicationWindow {
    id: window

    title: "Lampje"
    width: Kirigami.Units.gridUnit * 50
    height: Kirigami.Units.gridUnit * 36
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 20

    pageStack.defaultColumnWidth: Kirigami.Units.gridUnit * 24
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.initialPage: hue.configured ? Qt.resolvedUrl("pages/RoomsPage.qml")
                                          : Qt.resolvedUrl("pages/SetupPage.qml")

    // Opens a page in the column after `page`, closing what was there.
    function openAfter(page, url, properties) {
        var index = page.Kirigami.ColumnView.index
        while (pageStack.depth > index + 1)
            pageStack.pop()
        return pageStack.push(url, properties || {})
    }

    function openForm(url, properties) {
        return pageStack.layers.push(url, properties || {})
    }

    function closeForm() {
        pageStack.layers.pop()
    }

    function restart(url) {
        pageStack.layers.clear()
        pageStack.clear()
        pageStack.push(url)
    }

    Connections {
        target: hue
        function onPaired() { window.restart(Qt.resolvedUrl("pages/RoomsPage.qml")) }
        function onConfiguredChanged() {
            if (!hue.configured)
                window.restart(Qt.resolvedUrl("pages/SetupPage.qml"))
        }
    }

    // Errors from commands, while connected (connection trouble shows on the rooms page).
    Kirigami.InlineMessage {
        id: errorBanner
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Kirigami.Units.largeSpacing
        }
        z: 100
        type: Kirigami.MessageType.Error
        showCloseButton: true
        text: hue.lastError
        visible: false
        position: Kirigami.InlineMessage.Position.Inline

        Connections {
            target: hue
            function onLastErrorChanged() {
                errorBanner.visible = hue.lastError !== "" && hue.state === HueClient.Connected
            }
        }
    }
}
