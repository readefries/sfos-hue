.pragma library

// The kinds a room or zone can be, as offered when editing one (the bridge knows more).
function list() {
    return [
        { id: "living_room", name: qsTr("Living room") },
        { id: "kitchen", name: qsTr("Kitchen") },
        { id: "dining", name: qsTr("Dining") },
        { id: "bedroom", name: qsTr("Bedroom") },
        { id: "kids_bedroom", name: qsTr("Kids' bedroom") },
        { id: "guest_room", name: qsTr("Guest room") },
        { id: "bathroom", name: qsTr("Bathroom") },
        { id: "office", name: qsTr("Office") },
        { id: "hallway", name: qsTr("Hallway") },
        { id: "garden", name: qsTr("Garden") },
        { id: "home", name: qsTr("Home") },
        { id: "other", name: qsTr("Other") }
    ]
}

// Index of archetype in list(); one the list doesn't offer is "Other" there.
function indexOf(archetype) {
    var kinds = list()
    for (var i = 0; i < kinds.length; ++i) {
        if (kinds[i].id === archetype)
            return i
    }
    return kinds.length - 1
}
