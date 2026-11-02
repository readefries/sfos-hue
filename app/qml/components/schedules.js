.pragma library
// Words for the bridge's schedules (see ScheduleModel); qsTr works here as in QML.

// The bridge's order of days, as in ScheduleModel's days.
var weekDays = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]

// Hue's days in the order the locale starts its week; Qt counts days from Sunday (0).
function daysInLocaleOrder(locale) {
    var first = locale.firstDayOfWeek // 0 = Sunday ... 6 = Saturday
    var days = []
    for (var i = 0; i < 7; ++i) {
        var qtDay = (first + i) % 7
        days.push({ key: weekDays[(qtDay + 6) % 7], name: locale.dayName(qtDay, 0) })
    }
    return days
}

function kindName(kind, scriptName) {
    switch (kind) {
    case "wake_up": return qsTr("Wake up")
    case "go_to_sleep": return qsTr("Go to sleep")
    case "timer": return qsTr("Timer")
    case "time_based_light": return qsTr("Lights on a schedule")
    case "natural_light": return qsTr("Natural light")
    case "coming_home": return qsTr("Coming home")
    case "leaving_home": return qsTr("Leaving home")
    default: return scriptName
    }
}

function daysText(days, locale) {
    if (days.length === 0)
        return qsTr("Once")
    if (days.length === 7)
        return qsTr("Every day")
    var joined = days.join(",")
    if (joined === "monday,tuesday,wednesday,thursday,friday")
        return qsTr("Weekdays")
    if (joined === "saturday,sunday")
        return qsTr("Weekends")
    var names = []
    var ordered = daysInLocaleOrder(locale)
    for (var i = 0; i < ordered.length; ++i) {
        if (days.indexOf(ordered[i].key) >= 0) {
            var qtDay = (weekDays.indexOf(ordered[i].key) + 1) % 7
            names.push(locale.dayName(qtDay, 1)) // Locale.ShortFormat
        }
    }
    return names.join(", ")
}

// formatTime: function(hour, minute) giving the locale's way of writing it.
function startText(timeType, hour, minute, offset, formatTime) {
    if (timeType === "time" && hour >= 0)
        return formatTime(hour, minute)
    var base
    if (timeType === "sunrise")
        base = qsTr("Sunrise")
    else if (timeType === "sunset")
        base = qsTr("Sunset")
    else
        return ""
    if (offset > 0)
        return qsTr("%1 + %n min", "", offset).arg(base)
    if (offset < 0)
        return qsTr("%1 − %n min", "", -offset).arg(base)
    return base
}

function durationText(seconds) {
    if (seconds < 0)
        return ""
    if (seconds < 60)
        return qsTr("%n s", "", seconds)
    var minutes = Math.round(seconds / 60)
    if (minutes < 60)
        return qsTr("%n min", "", minutes)
    var hours = Math.floor(minutes / 60)
    var rest = minutes % 60
    return rest === 0 ? qsTr("%n h", "", hours) : qsTr("%1 h %2 min").arg(hours).arg(rest)
}

// One line: when it starts (and ends) and on which days, or how long a timer runs.
function whenText(s, locale, formatTime) {
    var parts = []
    var start = startText(s.timeType, s.hour, s.minute, s.offset, formatTime)
    if (start !== "") {
        if (s.endHour >= 0)
            start = qsTr("%1 to %2").arg(start).arg(formatTime(s.endHour, s.endMinute))
        parts.push(start)
        parts.push(daysText(s.days, locale))
    }
    if (s.duration >= 0)
        parts.push(durationText(s.duration))
    return parts.join(" · ")
}
