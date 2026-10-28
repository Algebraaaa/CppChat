import QtQuick
import "ChatStyle.js" as Style

Rectangle {
    id: card
    objectName: "fileMessageCard"
    required property var backend
    required property var message
    property real maximumWidth: 260
    implicitWidth: Math.min(maximumWidth, 236)
    implicitHeight: attachment.implicitHeight + 24
    radius: 8
    color: Style.surface
    border.color: Style.separator
    signal saveRequested(string fileId, string name)
    AttachmentMessage {
        id: attachment
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 12
        backend: card.backend
        message: card.message
        onSaveRequested: function(id, name) { card.saveRequested(id, name) }
    }
}
