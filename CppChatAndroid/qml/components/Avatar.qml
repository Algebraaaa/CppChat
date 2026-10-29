import QtQuick
import QtQuick.Controls.Material
import "ChatStyle.js" as Style

Rectangle {
    id: root
    required property var backend
    property int uid: 0
    property string fallback: ""
    property string name: "?"
    implicitWidth: 48
    implicitHeight: 48
    radius: 7
    color: Style.accent
    clip: true
    Label { anchors.centerIn: parent; text: root.name.charAt(0) || "?"; color: "white"; font.pixelSize: Math.min(root.width * 0.43, 24) }
    Image {
        id: picture
        asynchronous: true
        anchors.fill: parent
        sourceSize.width: 96
        sourceSize.height: 96
        fillMode: Image.PreserveAspectCrop
        source: root.uid > 0 ? root.backend.avatarSource(root.uid, root.fallback) : ""
    }
    Connections {
        target: root.backend
        function onAvatarReady(uid) {
            if (uid === root.uid) picture.source = root.backend.avatarSource(uid, root.fallback)
        }
    }
}
