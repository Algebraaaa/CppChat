import QtQuick
import QtQuick.Controls.Material
import "ChatStyle.js" as Style

Item {
    id: bubble
    required property var backend
    required property var message
    property real maximumWidth: 260
    readonly property bool outgoing: !!message.isSelf
    readonly property bool pictureMessage: message.type === 1
    readonly property real naturalWidth: message.type === 0 ? measure.implicitWidth + 28 :
                                                              pictureMessage ? 220 : 236
    implicitWidth: Math.min(maximumWidth, Math.max(48, naturalWidth))
    implicitHeight: Math.max(44, body.implicitHeight + 24)
    signal saveRequested(string fileId, string name)
    signal imageRequested(string fileId)

    Text {
        id: measure
        visible: false
        text: bubble.message.content || ""
        textFormat: Text.PlainText
        font: messageText.font
    }
    Rectangle {
        anchors.fill: parent
        radius: 5
        color: bubble.outgoing ? Style.outgoing : Style.surface
    }
    Rectangle {
        width: 8
        height: 8
        y: 16
        rotation: 45
        x: bubble.outgoing ? bubble.width - 5 : -3
        color: bubble.outgoing ? Style.outgoing : Style.surface
    }
    Column {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 6
        TextArea {
            id: messageText
            objectName: "messageText"
            visible: bubble.message.type === 0
            width: parent.width
            height: visible ? implicitHeight : 0
            padding: 0
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            text: bubble.message.type === 0 ? (bubble.message.content || "") : ""
            color: Style.text
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            font.pixelSize: 17
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            persistentSelection: true
            selectionColor: "#55b87d"
            selectedTextColor: "white"
            background: null
            onPressAndHold: function (event) {
                messageMenu.popup();
                event.accepted = true;
            }
            Menu {
                id: messageMenu
                objectName: "messageMenu"
                popupType: Popup.Item
                MenuItem {
                    text: qsTr("复制")
                    onTriggered: {
                        if (!messageText.selectedText.length)
                            messageText.selectAll();
                        messageText.copy();
                        messageText.deselect();
                    }
                }
            }
        }
        Item {
            visible: bubble.pictureMessage
            width: parent.width
            height: visible ? Math.min(240, Math.max(100, width * (picture.sourceSize.width > 0
                                                                   ? picture.sourceSize.height
                                                                     / picture.sourceSize.width :
                                                                     0.65))) : 0
            Image {
                id: picture
                objectName: "messageImage"
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize.width: 640
                sourceSize.height: 640
                source: bubble.pictureMessage && bubble.message.msgId > 0
                        ? bubble.backend.imageSource(bubble.message.content) : ""
                TapHandler {
                    enabled: picture.status === Image.Ready
                    onTapped: bubble.imageRequested(bubble.message.content)
                }
            }
            Column {
                anchors.centerIn: parent
                width: parent.width
                spacing: 8
                visible: picture.status !== Image.Ready
                ChatIcon {
                    name: "image"
                    color: Style.secondary
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Label {
                    width: parent.width
                    text: bubble.message.msgId === 0 ? qsTr("图片等待发送") : picture.status
                                                       === Image.Error ? qsTr("图片暂不可用") : qsTr(
                                                                             "正在加载图片…")
                    color: Style.secondary
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 12
                }
            }
        }
        AttachmentMessage {
            width: parent.width
            visible: bubble.message.type === 2 || bubble.message.type === 3
            height: visible ? implicitHeight : 0
            backend: bubble.backend
            message: bubble.message
            onSaveRequested: function (fileId, name) {
                bubble.saveRequested(fileId, name);
            }
        }
        Label {
            visible: bubble.message.type < 0 || bubble.message.type > 3
            width: parent.width
            text: qsTr("暂不支持此消息，请更新客户端")
            color: Style.secondary
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: bubble.backend
        function onImageReady(fileId) {
            if (bubble.pictureMessage && fileId === bubble.message.content)
                picture.source = bubble.backend.imageSource(fileId);
        }
    }
}
