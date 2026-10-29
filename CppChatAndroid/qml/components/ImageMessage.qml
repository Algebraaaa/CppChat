import QtQuick
import QtQuick.Controls.Material
import "ChatStyle.js" as Style

Item {
    id: imageMessage
    objectName: "imageMessageSurface"
    required property var backend
    required property var message
    property real maximumWidth: 260
    implicitWidth: Math.min(maximumWidth, 220)
    implicitHeight: Math.min(240, Math.max(100, width * (picture.implicitWidth > 0
                          ? picture.implicitHeight / picture.implicitWidth : 0.65)))
    signal imageRequested(string fileId)
    Image {
        id: picture
        objectName: "messageImage"
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        sourceSize.width: 640; sourceSize.height: 640
        source: imageMessage.message.content ? imageMessage.backend.imageSource(imageMessage.message.content) : ""
        TapHandler {
            enabled: picture.status === Image.Ready
            onTapped: imageMessage.imageRequested(imageMessage.message.content)
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: picture.status !== Image.Ready
        radius: 7
        color: Style.surface
        border.color: Style.separator
        Column {
            anchors.centerIn: parent
            width: parent.width - 16
            spacing: 8
            ChatIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "image"; color: Style.secondary }
            Label {
                width: parent.width
                text: imageMessage.message.msgId === 0 ? qsTr("图片等待发送")
                      : picture.status === Image.Error ? qsTr("图片暂不可用") : qsTr("正在加载图片…")
                horizontalAlignment: Text.AlignHCenter
                color: Style.secondary
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
    }
    Connections {
        target: imageMessage.backend
        function onImageReady(id) {
            if (id === imageMessage.message.content)
                picture.source = imageMessage.backend.imageSource(id)
        }
    }
}
