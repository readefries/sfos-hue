import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import harbour.lampje 1.0
import "../components"

Kirigami.ScrollablePage {
    id: page

    property string groupId
    property string groupName
    property string groupKind
    property string groupArchetype
    // Rooms and zones of the bridge, rather than Lampje's own ("local:...").
    readonly property bool fromBridge: hue.isFromBridge(groupId)

    title: groupName

    LightModel {
        id: lights
        store: hue.store
        groupId: page.groupId
    }

    SceneModel {
        id: scenes
        store: hue.store
        groupId: page.groupId
    }

    function editScene(sceneId, sceneName) {
        applicationWindow().openForm(Qt.resolvedUrl("SceneDialog.qml"), {
                                         groupId: page.groupId,
                                         groupName: page.groupName,
                                         sceneId: sceneId,
                                         sceneName: sceneName
                                     })
    }

    actions: [
        Kirigami.Action {
            text: qsTr("New scene from current lights")
            icon.name: "list-add"
            visible: page.fromBridge
            enabled: hue.state === HueClient.Connected
            onTriggered: page.editScene("", "")
        },
        Kirigami.Action {
            text: qsTr("Edit room")
            icon.name: "document-edit"
            visible: page.groupKind === "room"
            onTriggered: {
                var form = applicationWindow().openForm(Qt.resolvedUrl("EditDialog.qml"), {
                                                            title: qsTr("Edit room"),
                                                            name: page.groupName,
                                                            archetype: page.groupArchetype,
                                                            withKind: true
                                                        })
                form.accepted.connect(function() {
                    hue.updateRoom(page.groupId, form.newName, form.newArchetype)
                })
            }
        },
        Kirigami.Action {
            text: qsTr("Edit zone")
            icon.name: "document-edit"
            visible: page.groupKind === "zone"
            onTriggered: applicationWindow().openForm(Qt.resolvedUrl("ZoneDialog.qml"), {
                                                          zoneId: page.groupId,
                                                          zoneName: page.groupName,
                                                          zoneArchetype: page.groupArchetype
                                                      })
        },
        Kirigami.Action {
            text: page.groupKind === "zone" ? qsTr("Delete zone") : qsTr("Delete room")
            icon.name: "edit-delete"
            visible: !page.fromBridge
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: confirmDelete.open()
        }
    ]

    Kirigami.PromptDialog {
        id: confirmDelete
        title: page.groupKind === "zone" ? qsTr("Delete zone") : qsTr("Delete room")
        subtitle: page.groupName
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onAccepted: {
            var id = page.groupId
            applicationWindow().pageStack.pop()
            hue.deleteGroup(id)
        }
    }

    ListView {
        id: list
        model: lights

        header: ColumnLayout {
            width: list.width
            spacing: 0

            // The room itself, as a one-row model so it stays live.
            Repeater {
                model: RoomModel {
                    store: hue.store
                    groupId: page.groupId
                }
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Follow renames (e.g. of a zone, from its form).
                    property string liveName: model.name
                    property string liveArchetype: model.archetype
                    onLiveNameChanged: page.groupName = liveName
                    onLiveArchetypeChanged: page.groupArchetype = liveArchetype
                    Component.onCompleted: {
                        page.groupName = liveName
                        page.groupArchetype = liveArchetype
                    }

                    QQC2.Switch {
                        text: qsTr("On")
                        enabled: model.lightCount > 0
                        checked: model.on
                        onToggled: {
                            checked = Qt.binding(function() { return model.on })
                            hue.setOn(model.resourceId, !model.on)
                        }
                    }
                    QQC2.Label {
                        visible: model.dimmable
                        text: qsTr("Brightness")
                    }
                    LevelSlider {
                        Layout.fillWidth: true
                        visible: model.dimmable
                        from: 1
                        to: 100
                        level: model.brightness
                        onChangeRequested: (value) => hue.setBrightness(model.resourceId, value)
                    }
                }
            }

            Kirigami.ListSectionHeader {
                Layout.fillWidth: true
                text: qsTr("Scenes")
                visible: scenes.count > 0
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: scenes.count > 0
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                text: qsTr("Click to switch to a scene; right-click to change it")
            }

            GridLayout {
                id: sceneGrid
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                columns: Math.max(2, Math.floor(width / (Kirigami.Units.gridUnit * 9)))
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: scenes
                    delegate: QQC2.AbstractButton {
                        id: tile
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                        hoverEnabled: true
                        padding: Kirigami.Units.largeSpacing
                        onClicked: hue.recallScene(model.resourceId)

                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        QQC2.ToolTip.text: qsTr("Right-click to change or delete")

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: page.editScene(model.resourceId, model.name)
                        }

                        background: Rectangle {
                            radius: Kirigami.Units.cornerRadius
                            color: Qt.rgba(model.sceneColor.r, model.sceneColor.g, model.sceneColor.b,
                                           model.active ? 0.6 : (tile.hovered ? 0.4 : 0.25))
                            border.width: model.active ? 2 : 0
                            border.color: Kirigami.Theme.highlightColor
                        }
                        contentItem: QQC2.Label {
                            text: model.name
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Kirigami.ListSectionHeader {
                Layout.fillWidth: true
                text: qsTr("Lights")
                visible: lights.count > 0
            }
        }

        delegate: LightDelegate {
            onClicked: applicationWindow().openAfter(page, Qt.resolvedUrl("LightPage.qml"), {
                                                         lightId: model.resourceId,
                                                         lightName: model.name
                                                     })
        }
    }
}
