import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.lampje 1.0
import "../components"

Page {
    id: page

    property string groupId
    property string groupName
    property string groupKind
    property string groupArchetype

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

    SilicaListView {
        id: list
        anchors.fill: parent
        model: lights

        PullDownMenu {
            MenuItem {
                text: qsTr("Edit zone")
                visible: page.groupKind === "zone"
                onClicked: pageStack.push(Qt.resolvedUrl("ZoneDialog.qml"), {
                                              zoneId: page.groupId,
                                              zoneName: page.groupName,
                                              zoneArchetype: page.groupArchetype
                                          })
            }
            MenuItem {
                text: qsTr("New scene from current lights")
                enabled: hue.state === HueClient.Connected
                onClicked: pageStack.push(Qt.resolvedUrl("SceneDialog.qml"), {
                                              groupId: page.groupId,
                                              groupName: page.groupName
                                          })
            }
        }

        header: Column {
            width: list.width

            PageHeader {
                title: page.groupName
            }

            // The room itself, as a one-row model so it stays live.
            Repeater {
                model: RoomModel {
                    store: hue.store
                    groupId: page.groupId
                }
                delegate: Column {
                    width: list.width

                    // Follow renames (e.g. of a zone, from its dialog).
                    property string liveName: model.name
                    property string liveArchetype: model.archetype
                    onLiveNameChanged: page.groupName = liveName
                    onLiveArchetypeChanged: page.groupArchetype = liveArchetype
                    Component.onCompleted: {
                        page.groupName = liveName
                        page.groupArchetype = liveArchetype
                    }

                    TextSwitch {
                        text: qsTr("On")
                        enabled: model.groupedLightId !== ""
                        checked: model.on
                        automaticCheck: false
                        onClicked: hue.setOn(model.groupedLightId, !model.on)
                    }
                    LevelSlider {
                        label: qsTr("Brightness")
                        visible: model.groupedLightId !== ""
                        minimumValue: 1
                        maximumValue: 100
                        stepSize: 1
                        valueText: Math.round(value) + " %"
                        level: model.brightness
                        onChangeRequested: hue.setBrightness(model.groupedLightId, value)
                    }
                }
            }

            SectionHeader {
                text: qsTr("Scenes")
                visible: scenes.count > 0
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: scenes.count > 0
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Tap to switch to a scene; press and hold to change it")
            }

            Grid {
                id: sceneGrid
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                columns: page.isPortrait ? 2 : 4
                spacing: Theme.paddingMedium

                Repeater {
                    model: scenes
                    delegate: BackgroundItem {
                        id: tile
                        width: (sceneGrid.width - (sceneGrid.columns - 1) * sceneGrid.spacing) / sceneGrid.columns
                        height: Theme.itemSizeMedium
                        onClicked: hue.recallScene(model.resourceId)
                        onPressAndHold: pageStack.push(Qt.resolvedUrl("SceneDialog.qml"), {
                                                           groupId: page.groupId,
                                                           groupName: page.groupName,
                                                           sceneId: model.resourceId,
                                                           sceneName: model.name
                                                       })

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.paddingSmall
                            color: model.sceneColor
                            opacity: model.active ? 0.6 : 0.25
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.paddingSmall
                            color: "transparent"
                            border.width: model.active ? 2 : 0
                            border.color: Theme.highlightColor
                        }
                        Label {
                            anchors {
                                fill: parent
                                margins: Theme.paddingMedium
                            }
                            verticalAlignment: Text.AlignVCenter
                            text: model.name
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeSmall
                            color: tile.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                }
            }

            SectionHeader {
                text: qsTr("Lights")
                visible: lights.count > 0
            }
        }

        delegate: LightDelegate {}

        VerticalScrollDecorator {}
    }
}
