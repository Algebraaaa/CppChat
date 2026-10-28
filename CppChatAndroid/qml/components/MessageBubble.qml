import QtQuick
import "ChatStyle.js" as Style

// Route content by type; media does not inherit the text bubble's background.
Item {
    id: messageView
    required property var backend
    required property var message
    property real maximumWidth: 260
    readonly property bool outgoing: !!message.isSelf
    readonly property bool pictureMessage: message.type === 1
    implicitWidth: content.item ? content.item.implicitWidth : Math.min(maximumWidth, 236)
    implicitHeight: content.item ? content.item.implicitHeight : 44
    signal saveRequested(string fileId, string name)
    signal imageRequested(string fileId)
    Loader {
        id: content
        anchors.fill: parent
        sourceComponent: messageView.pictureMessage ? imageContent
                         : messageView.message.type === 2 ? fileContent
                         : messageView.message.type === 3 ? voiceContent : textContent
    }
    Component {
        id: textContent
        TextMessage { message: messageView.message; maximumWidth: messageView.maximumWidth }
    }
    Component {
        id: imageContent
        ImageMessage {
            backend: messageView.backend
            message: messageView.message
            maximumWidth: messageView.maximumWidth
            onImageRequested: function(id) { messageView.imageRequested(id) }
        }
    }
    Component {
        id: fileContent
        FileMessageCard {
            backend: messageView.backend
            message: messageView.message
            maximumWidth: messageView.maximumWidth
            onSaveRequested: function(id, name) { messageView.saveRequested(id, name) }
        }
    }
    Component {
        id: voiceContent
        Rectangle {
            objectName: "voiceMessageSurface"
            implicitWidth: Math.min(messageView.maximumWidth, 236)
            implicitHeight: voice.implicitHeight + 24
            radius: 8
            color: messageView.outgoing ? Style.outgoing : Style.surface
            AttachmentMessage {
                id: voice
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 12
                backend: messageView.backend
                message: messageView.message
            }
        }
    }
}
