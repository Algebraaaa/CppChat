import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "ChatStyle.js" as Style

ColumnLayout {
    id: content
    objectName: "attachmentContent"
    required property var backend
    required property var message
    property var info: ({ready: false, name: "", size: 0})
    property string progress: ""
    property bool loading: false
    signal saveRequested(string fileId, string name)
    function resetAttachment() {
        if (!backend || !message) return
        loading = false
        progress = ""
        info = message.type === 2 && message.msgId > 0 ? backend.fileInfo(message.content)
                                                    : ({ready: false, name: "", size: 0})
    }
    onMessageChanged: resetAttachment()
    Component.onCompleted: resetAttachment()
    spacing: 6
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 36; Layout.preferredHeight: 40
            color: content.message.type === 3 ? "transparent" : "#f4a340"
            radius: 5
            ChatIcon { anchors.centerIn: parent; name: content.message.type === 3 ? "voice" : "file"; color: content.message.type === 3 ? Style.text : "white" }
        }
        Label {
            Layout.fillWidth: true
            color: Style.text
            font.pixelSize: 15
            text: content.message.type === 3 ? qsTr("语音消息") : (content.info.ready ? content.info.name : qsTr("文件附件"))
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }
    Label {
        Layout.fillWidth: true
        visible: text.length > 0
        text: content.progress || (content.info.ready ? qsTr("%1 KiB").arg((content.info.size / 1024).toFixed(1)) : "")
        font.pixelSize: 11
        color: Style.secondary
        wrapMode: Text.Wrap
    }
    Button {
        id: attachmentAction
        objectName: "attachmentAction"
        Layout.fillWidth: true
        implicitHeight: 40
        text: content.message.type === 3 ? (content.backend.media.playing === content.message.content ? qsTr("停止播放") : qsTr("播放语音"))
            : content.loading ? qsTr("正在下载…") : content.info.ready ? qsTr("另存为…") : qsTr("下载文件")
        enabled: content.message.msgId > 0 && !content.loading && !content.backend.media.recording && content.backend.media.call.state === 0
        topInset: 0; bottomInset: 0
        background: Rectangle { radius: 5; color: attachmentAction.down ? "#18000000" : "#08000000" }
        contentItem: Label { text: attachmentAction.text; color: attachmentAction.enabled ? Style.text : Style.secondary; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        onClicked: {
            if (content.message.type === 3) {
                content.backend.media.playVoice(content.message.content)
            } else if (content.info.ready) {
                content.saveRequested(content.message.content, content.info.name)
            } else {
                content.loading = true
                content.backend.downloadFile(content.message.content)
            }
        }
    }
    Connections {
        target: content.backend
        function onFileReady(id) {
            if (id === content.message.content) {
                content.loading = false
                content.progress = ""
                content.info = content.backend.fileInfo(id)
            }
        }
        function onImageReady(id) {
            if (id === content.message.content && content.message.type === 2) {
                content.loading = false
                content.info = content.backend.fileInfo(id)
            }
        }
        function onOperationFailed(message) { content.loading = false }
        function onLoginStatusChanged() { if (!content.backend.isLoggedIn) content.loading = false }
        function onTransferProgress(id, done, total, upload) {
            if ((upload && id === content.message.uniqueId) || (!upload && id === content.message.content))
                content.progress = qsTr("%1 %2% · %3/%4 KiB").arg(upload ? qsTr("上传") : qsTr("下载")).arg(total > 0 ? Math.floor(done * 100 / total) : 0).arg(Math.floor(done / 1024)).arg(Math.ceil(total / 1024))
        }
    }
}
