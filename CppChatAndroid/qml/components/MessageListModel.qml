import QtQuick

// Keep delegate identity stable across delivery receipts and incoming messages.
ListModel {
    function apply(rows) {
        const known = Object.create(null)
        for (let i = 0; i < count; ++i) known[get(i).key] = true
        for (let i = 0; i < rows.length; ++i) {
            const message = rows[i]
            const key = message.uniqueId || ("id:" + message.msgId)
            const previousTime = i > 0 ? rows[i - 1].time : ""
            const revision = JSON.stringify([message.msgId, message.senderUid, message.content,
                message.time, message.isSelf, message.type, message.status, previousTime])
            if (i >= count || get(i).key !== key) {
                let found = -1
                if (known[key]) {
                    for (let j = i + 1; j < count; ++j) {
                        if (get(j).key === key) { found = j; break }
                    }
                }
                if (found >= 0) move(found, i, 1)
                else {
                    insert(i, {key: key, revision: revision, payload: message,
                               timeBefore: previousTime})
                    known[key] = true
                }
            }
            if (get(i).revision !== revision)
                set(i, {key: key, revision: revision, payload: message,
                        timeBefore: previousTime})
        }
        if (count > rows.length) remove(rows.length, count - rows.length)
    }
}
