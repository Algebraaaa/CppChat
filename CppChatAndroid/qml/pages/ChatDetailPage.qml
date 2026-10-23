import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"

Page {
    id: page
    required property var backend
    required property var navigation
    property int threadId: 0
    property var messages: []
    property bool olderAvailable: true
    title: qsTr("聊天")
    background: Rectangle { color: "#ededed" }
    header: MobileHeader { title: page.title; onBack: page.navigation.back(); actionText: "⋯"; onAction: actions.open() }
    Menu {
        id: actions; x: Math.max(0, page.width - width)
        MenuItem { text: qsTr("语音通话"); visible: !page.backend.threadInfo(page.threadId).isGroup; enabled: page.backend.isLoggedIn && page.backend.mediaCapabilities.call; onTriggered: page.backend.startCall(page.threadId, false) }
        MenuItem { text: qsTr("视频通话"); visible: !page.backend.threadInfo(page.threadId).isGroup; enabled: page.backend.isLoggedIn && page.backend.mediaCapabilities.video; onTriggered: page.backend.startCall(page.threadId, true) }
        MenuItem { text: qsTr("搜索聊天记录"); onTriggered: page.navigation.push("history", {threadId: page.threadId}) }
        MenuItem { text: page.backend.threadInfo(page.threadId).pinned ? qsTr("取消置顶") : qsTr("置顶会话"); onTriggered: page.backend.setPinned(page.threadId, !page.backend.threadInfo(page.threadId).pinned) }
        MenuItem { visible: !!page.backend.threadInfo(page.threadId).isGroup; text: qsTr("群聊管理"); onTriggered: page.navigation.push("group", {threadId: page.threadId}) }
    }

    function refresh() {
        const follow = messageList.count === 0 || messageList.atYEnd
        messageList.stickToEnd = follow
        const oldY = messageList.contentY
        const incoming = backend.messagesForThread(threadId)
        // Keep the expanded local history visible when the shared service
        // refreshes its latest-message window after an incoming message.
        let merged = ({})
        for (const message of page.messages)
            merged[message.uniqueId || ("id:" + message.msgId)] = message
        for (const message of incoming)
            merged[message.uniqueId || ("id:" + message.msgId)] = message
        page.messages = Object.values(merged).sort(function(a, b) {
            if (a.msgId === 0 || b.msgId === 0) {
                if (a.msgId !== b.msgId) return a.msgId === 0 ? 1 : -1
                return a.time.localeCompare(b.time)
            }
            return a.msgId - b.msgId
        })
        Qt.callLater(function() {
            messageList.forceLayout()
            if (follow) messageList.positionViewAtEnd()
            else messageList.contentY = oldY
        })
    }
    function send() {
        if (input.inputMethodComposing || backend.sending) return
        backend.sendTextMessage(threadId, input.text)
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ListView {
            id: messageList
            property bool stickToEnd: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 12
            topMargin: 12
            bottomMargin: 12
            model: page.messages
            ScrollBar.vertical: ScrollBar {}
            onMovementStarted: stickToEnd = false
            onMovementEnded: stickToEnd = atYEnd
            onHeightChanged: { if (stickToEnd) Qt.callLater(positionViewAtEnd) }
            onContentHeightChanged: { if (stickToEnd && !moving) Qt.callLater(positionViewAtEnd) }
            header: Button {
                width: messageList.width
                flat: true
                text: page.olderAvailable ? qsTr("加载更早的消息") : qsTr("已显示全部本地记录")
                enabled: page.olderAvailable
                onClicked: {
                    let before = 0
                    for (const message of page.messages) {
                        if (message.msgId > 0) { before = message.msgId; break }
                    }
                    page.backend.loadMessages(page.threadId, before, 50)
                }
            }
            delegate: Item {
                id: row
                required property var modelData
                width: messageList.width
                height: bubble.height + 24
                Avatar {
                    id: avatar
                    backend: page.backend
                    uid: row.modelData.senderUid
                    name: row.modelData.isSelf ? page.backend.currentNick : page.title
                    width: 36
                    height: 36
                    x: row.modelData.isSelf ? row.width - width - 12 : 12
                }
                Rectangle {
                    id: bubble
                    x: row.modelData.isSelf ? avatar.x - width - 8 : avatar.x + avatar.width + 8
                    width: Math.min(520, Math.max(80, row.width - 116))
                    height: content.implicitHeight + 24
                    color: row.modelData.isSelf ? "#b4ef92" : "white"
                    radius: 10
                    ColumnLayout {
                        id: content
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 6
                        Label {
                            visible: row.modelData.type === 0
                            Layout.fillWidth: true
                            text: row.modelData.content
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            font.pixelSize: 16
                        }
                        Image {
                            id: picture
                            visible: row.modelData.type === 1 && row.modelData.msgId > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? Math.min(240, bubble.width) : 0
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 640
                            sourceSize.height: 640
                            source: visible ? page.backend.imageSource(row.modelData.content) : ""
                            Connections {
                                target: page.backend
                                function onImageReady(fileId) {
                                    if (fileId === row.modelData.content) picture.source = page.backend.imageSource(fileId)
                                }
                            }
                        }
                        Label { visible: row.modelData.type === 1 && row.modelData.msgId === 0; text: qsTr("[图片] 正在发送或等待重试"); Layout.fillWidth: true; wrapMode: Text.Wrap }
                        AttachmentMessage {
                            Layout.fillWidth: true
                            visible: row.modelData.type === 2 || row.modelData.type === 3
                            backend: page.backend
                            message: row.modelData
                            onSaveRequested: function(fileId, name) {
                                saveDialog.fileId = fileId
                                saveDialog.selectedFile = "file:///" + encodeURIComponent(name)
                                saveDialog.open()
                            }
                        }
                        Label { text: row.modelData.time.slice(11, 16); font.pixelSize: 10; color: "#667085" }
                        Button {
                            visible: row.modelData.isSelf && row.modelData.msgId === 0 && row.modelData.status === 1
                            text: qsTr("发送失败，点击重试")
                            implicitHeight: 48
                            Layout.fillWidth: true
                            onClicked: page.backend.retryMessage(page.threadId, row.modelData.uniqueId)
                        }
                    }
                }
            }
        }
        Pane {
            visible: page.backend.media.recordingThread === page.threadId && (page.backend.media.recording || page.backend.media.draftMs > 0 || page.backend.media.permissionPending)
            Layout.fillWidth: true
            padding: 8
            ColumnLayout {
                width: parent.width
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: page.backend.media.permissionPending ? qsTr("等待麦克风授权…") : page.backend.media.recording ? qsTr("正在录音 %1 秒（最长 60 秒）").arg((page.backend.media.recordingMs / 1000).toFixed(1)) : qsTr("语音 %1 秒，发送前可取消").arg((page.backend.media.draftMs / 1000).toFixed(1)) }
                RowLayout {
                    Button { text: qsTr("取消"); enabled: !page.backend.sending; onClicked: page.backend.media.discardRecording() }
                    Button { text: page.backend.media.recording ? qsTr("完成录音") : qsTr("发送语音"); enabled: !page.backend.media.permissionPending && !page.backend.sending; onClicked: page.backend.media.recording ? page.backend.media.finishRecording() : page.backend.sendVoice(page.threadId) }
                }
            }
        }
        Pane {
            Layout.fillWidth: true
            padding: 8
            RowLayout {
                width: parent.width
                spacing: 8
                ToolButton {
                    text: "+"
                    font.pixelSize: 26
                    implicitWidth: 48
                    implicitHeight: 48
                    enabled: !page.backend.sending
                    onClicked: attachments.open()
                    Accessible.name: qsTr("发送图片、文件或语音")
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(132, Math.max(48, input.implicitHeight))
                    contentWidth: availableWidth
                    TextArea {
                        id: input
                        placeholderText: qsTr("输入消息…")
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        font.pixelSize: 16
                        onTextChanged: draftTimer.restart()
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                                page.send()
                                event.accepted = true
                            }
                        }
                    }
                }
                PrimaryButton {
                    text: page.backend.sending ? qsTr("处理中") : qsTr("发送")
                    enabled: input.text.trim().length > 0 && !page.backend.sending && page.backend.isLoggedIn
                    implicitHeight: 52
                    Material.background: "#07a956"
                    Material.foreground: "white"
                    onClicked: page.send()
                }
            }
        }
    }
    FileDialog {
        id: imageDialog
        title: qsTr("选择要发送的图片")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: page.backend.sendImage(page.threadId, selectedFile)
    }
    Menu {
        id: attachments
        y: Math.max(0, page.height - height - 64)
        MenuItem { text: qsTr("图片"); onTriggered: imageDialog.open() }
        MenuItem { text: qsTr("文件（最多 20 MiB）"); enabled: page.backend.mediaCapabilities.files && page.backend.isLoggedIn; onTriggered: fileDialog.open() }
        MenuItem { text: qsTr("录制语音"); enabled: page.backend.mediaCapabilities.voice && page.backend.isLoggedIn && !page.backend.media.recording && page.backend.media.draftMs === 0 && page.backend.media.call.state === 0; onTriggered: page.backend.startRecording(page.threadId) }
    }
    FileDialog {
        id: fileDialog
        title: qsTr("选择要发送的文件")
        fileMode: FileDialog.OpenFile
        onAccepted: page.backend.sendFile(page.threadId, selectedFile)
    }
    FileDialog {
        id: saveDialog
        property string fileId: ""
        title: qsTr("保存收到的文件")
        fileMode: FileDialog.SaveFile
        onAccepted: page.backend.saveFile(fileId, selectedFile)
    }
    Timer { id: draftTimer; interval: 500; onTriggered: page.backend.saveDraft(page.threadId, input.text) }
    Component.onDestruction: { page.backend.media.leaveChat(page.threadId); if (page.backend.storeReady) page.backend.saveDraft(page.threadId, input.text) }
    StackView.onDeactivating: page.backend.media.leaveChat(page.threadId)
    Component.onCompleted: { refresh(); backend.loadMessages(threadId); backend.loadDraft(threadId) }
    Connections {
        target: page.backend
        function onMessagesLoaded(thread) { if (thread === page.threadId) page.refresh() }
        function onHistoryCleared(thread) { if (thread === page.threadId) { page.messages = []; page.refresh() } }
        function onDraftLoaded(thread, text) { if (thread === page.threadId && !input.text.length) input.text = text }
        function onTextMessageSaved(thread, text) {
            if (thread !== page.threadId) return
            if (input.text === text) input.clear()
            messageList.stickToEnd = true
            Qt.callLater(messageList.positionViewAtEnd)
        }
        function onLocalHistoryLoaded(thread, more) { if (thread === page.threadId) page.olderAvailable = more }
    }
}
