.pragma library

// How a cover action ({kind, name, groupName}, from hue.coverActions) is described in Settings.
function describe(action) {
    switch (action ? action.kind : "none") {
    case "alloff": return qsTr("All lights off")
    case "group": return qsTr("Switch %1 on or off").arg(action.name)
    case "scene": return qsTr("%1 in %2").arg(action.name).arg(action.groupName)
    default: return qsTr("Nothing")
    }
}
