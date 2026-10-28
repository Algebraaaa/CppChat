import QtQuick
import QtQuick.Controls.Material
import "ChatStyle.js" as Style

Rectangle {
    id: textBubble
    objectName: "textMessageSurface"
    required property var message
    property real maximumWidth: 260
    readonly property string displayText: message.type === 0 ? (message.content || "")
                                                            : qsTr("暂不支持此消息，请更新客户端")
    implicitWidth: Math.min(maximumWidth, Math.max(48, measure.implicitWidth + 28))
    implicitHeight: Math.max(44, messageText.implicitHeight + 24)
    radius: 5
    color: message.isSelf ? Style.outgoing : Style.surface
    Text {
        id: measure
        visible: false
        text: textBubble.displayText
        textFormat: Text.PlainText
        font: messageText.font
    }
    Rectangle {
        objectName: "textMessageTail"
        width: 8; height: 8; y: 16; rotation: 45
        x: textBubble.message.isSelf ? textBubble.width - 5 : -3
        color: textBubble.color
    }
    TextArea {
        id: messageText
        objectName: "messageText"
        x: 12; y: 12
        width: Math.max(0, parent.width - 24)
        height: implicitHeight
        padding: 0
        leftPadding: 0; rightPadding: 0
        topPadding: 0; bottomPadding: 0
        text: textBubble.displayText
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
        onPressAndHold: function(event) { messageMenu.popup(); event.accepted = true }
        Menu {
            id: messageMenu
            objectName: "messageMenu"
            popupType: Popup.Item
            MenuItem {
                text: qsTr("复制")
                onTriggered: {
                    if (!messageText.selectedText.length) messageText.selectAll()
                    messageText.copy()
                    messageText.deselect()
                }
            }
        }
    }
}
