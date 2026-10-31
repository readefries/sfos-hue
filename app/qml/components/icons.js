.pragma library

// Icon files for archetypes, shared by the lists and the cover. Paths are relative to the QML
// file using them, which is always one level below icons/ (components/, cover/).

function room(archetype) {
    var kind = "home"
    if (/living|lounge|recreation|man_cave|tv|music|reading/.test(archetype))
        kind = "sofa"
    else if (/kitchen|dining|barbecue/.test(archetype))
        kind = "kitchen"
    else if (/bed|nursery|guest/.test(archetype))
        kind = "bed"
    else if (/bath|toilet|laundry|pool/.test(archetype))
        kind = "bath"
    else if (/office|computer|studio|gym/.test(archetype))
        kind = "desk"
    else if (/hall|door|stair|closet|storage|garage|carport/.test(archetype))
        kind = "door"
    else if (/garden|terrace|balcony|porch|driveway/.test(archetype))
        kind = "garden"
    return "../icons/room-" + kind + ".svg"
}

function light(archetype) {
    var kind = "bulb"
    if (/spot|recessed|flood/.test(archetype))
        kind = "spot"
    else if (/^floor/.test(archetype))
        kind = "floor"
    else if (/^table|desk|^wall/.test(archetype))
        kind = "table"
    else if (/^pendant/.test(archetype))
        kind = "pendant"
    else if (/^ceiling/.test(archetype))
        kind = "ceiling"
    else if (/strip|play|bar|tube|gradient/.test(archetype))
        kind = "strip"
    return "../icons/light-" + kind + ".svg"
}

// What a cover action ({kind, archetype}, from hue.coverActions) shows.
function coverAction(action) {
    if (action.kind === "group")
        return room(action.archetype)
    if (action.kind === "scene")
        return "../icons/scene.svg"
    return "../icons/power.svg"
}
