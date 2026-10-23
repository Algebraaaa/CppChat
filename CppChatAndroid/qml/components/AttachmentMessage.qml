import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: content
    required property var backend
    required property var message
    property var info: ({ready: false, name: "", size: 0})
    property string progress: ""
    property bool loading: false
    signal saveRequested(string fileId, string name)
    spacing: 4
    Label {
        Layout.fillWidth: true
        text: content.message.type === 3 ? qsTr("语音消息") : (content.info.ready ? content.info.name : qsTr("文件"))
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
    }
    Label {
        Layout.fillWidth: true
        visible: text.length > 0
        text: content.progress || (content.info.ready ? qsTr("%1 KiB").arg((content.info.size / 1024).toFixed(1)) : "")
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
    Button {
        Layout.fillWidth: true
        implicitHeight: 48
        text: content.message.type === 3 ? (content.backend.media.playing === content.message.content ? qsTr("停止播放") : qsTr("播放语音"))
            : content.loading ? qsTr("正在下载…") : content.info.ready ? qsTr("另存为…") : qsTr("下载文件")
        enabled: !content.loading && !content.backend.media.recording && content.backend.media.call.state === 0
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
