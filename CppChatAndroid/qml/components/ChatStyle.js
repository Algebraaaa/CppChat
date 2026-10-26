.pragma library

var background = "#ededed"
var surface = "#ffffff"
var toolbar = "#f7f7f7"
var text = "#191919"
var secondary = "#737373"
var separator = "#e5e5e5"
var accent = "#07a956"
var outgoing = "#95ec69"

function date(value) {
    if (!value) return new Date(NaN)
    return new Date(String(value).replace(" ", "T"))
}

function sameDay(a, b) {
    return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
            && a.getDate() === b.getDate()
}

function timeLabel(value, now) {
    var parsed = date(value)
    if (isNaN(parsed.getTime())) return ""
    now = now || new Date()
    var clock = Qt.formatTime(parsed, "HH:mm")
    if (sameDay(parsed, now)) return clock
    var yesterday = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1)
    if (sameDay(parsed, yesterday)) return qsTr("昨天") + " " + clock
    return Qt.formatDate(parsed, parsed.getFullYear() === now.getFullYear() ? "M月d日" : "yyyy年M月d日") + " " + clock
}

function listTime(value) {
    var parsed = date(value)
    if (isNaN(parsed.getTime())) return ""
    var now = new Date()
    if (sameDay(parsed, now)) return Qt.formatTime(parsed, "HH:mm")
    var yesterday = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1)
    if (sameDay(parsed, yesterday)) return qsTr("昨天")
    return Qt.formatDate(parsed, parsed.getFullYear() === now.getFullYear() ? "M/d" : "yyyy/M/d")
}

function showTime(current, previous) {
    if (!current || !current.time) return false
    if (!previous) return true
    var time = date(current.time)
    var earlier = date(previous.time)
    return !isNaN(time.getTime()) && (isNaN(earlier.getTime())
            || !sameDay(time, earlier) || Math.abs(time - earlier) >= 5 * 60 * 1000)
}
