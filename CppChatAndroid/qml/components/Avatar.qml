import QtQuick
import QtQuick.Controls.Material

Rectangle {
    id: root
    required property var backend
    property int uid: 0
    property string fallback: ""
    property string name: "?"
    implicitWidth: 48
    implicitHeight: 48
    radius: 8
    color: "#07a956"
    Label { anchors.centerIn: parent; text: root.name.charAt(0); color: "white"; font.pixelSize: 20 }
    Image {
        id: picture
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
