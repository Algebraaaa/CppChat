import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import "../components"
import "../components/ChatStyle.js" as Style

Page {
    id: page
    required property var backend
    required property var navigation
    property int threadId: 0
    property var messages: []
    onMessagesChanged: { if (messageModel) messageModel.apply(messages) }
    property bool olderAvailable: true
    property bool attachmentsVisible: false
    property var conversation: backend.threadInfo(threadId)
    property var contacts: backend.friendList()
    property var members: backend.groupInfo(threadId).members || []
    title: qsTr("聊天")
    background: Rectangle { color: Style.background }
    header: MobileHeader { title: page.title; onBack: page.navigation.back(); actionText: "⋯"; onAction: actions.open() }
    MessageListModel { id: messageModel }
    Menu {
        id: actions; x: Math.max(0, page.width - width)
        MenuItem { text: qsTr("语音通话"); visible: !page.backend.threadInfo(page.threadId).isGroup; enabled: page.backend.isLoggedIn && page.backend.mediaCapabilities.call; onTriggered: page.backend.startCall(page.threadId, false) }
        MenuItem { text: qsTr("视频通话"); visible: !page.backend.threadInfo(page.threadId).isGroup; enabled: page.backend.isLoggedIn && page.backend.mediaCapabilities.video; onTriggered: page.backend.startCall(page.threadId, true) }
        MenuItem { text: qsTr("搜索聊天记录"); onTriggered: page.navigation.push("history", {threadId: page.threadId}) }
        MenuItem { text: page.backend.threadInfo(page.threadId).pinned ? qsTr("取消置顶") : qsTr("置顶会话"); onTriggered: page.backend.setPinned(page.threadId, !page.backend.threadInfo(page.threadId).pinned) }
        MenuItem { visible: !!page.backend.threadInfo(page.threadId).isGroup; text: qsTr("群聊管理"); onTriggered: page.navigation.push("group", {threadId: page.threadId}) }
    }

    function refresh() {
        const follow = messageList.stickToEnd && !messageList.moving
        const incoming = backend.messagesForThread(threadId)
        // Keep the expanded local history visible when the shared service
        // refreshes its latest-message window after an incoming message.
        let merged = Object.create(null)
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
            if (follow && messageList.stickToEnd) followTimer.restart()
        })
    }
    function send() {
        if (input.inputMethodComposing || backend.sending || !input.text.trim().length || !backend.isLoggedIn) return
        backend.sendTextMessage(threadId, input.text)
    }
    function senderName(message) {
        if (message.isSelf) return backend.currentNick || backend.currentUser || qsTr("我")
        for (const contact of contacts) {
            if (contact.uid === message.senderUid) return contact.displayName
        }
        for (const member of members) {
            if (member.uid === message.senderUid) return member.nick || member.name || qsTr("群成员")
        }
        return conversation.isGroup ? qsTr("成员 %1").arg(message.senderUid) : page.title
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ListView {
            id: messageList
            property bool stickToEnd: true
            function keepAtEnd() {
                if (stickToEnd && !moving && !atYEnd) {
                    forceLayout()
                    positionViewAtEnd()
                }
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            objectName: "messageList"
            spacing: 16
            topMargin: 8
            footer: Item { height: 20 }
            model: messageModel
            reuseItems: true
            cacheBuffer: height / 2
            ScrollBar.vertical: ScrollBar {
                id: chatScrollBar
                objectName: "chatScrollBar"
                width: 4
                padding: 0
                interactive: false
                minimumSize: 0.04
                opacity: active ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                contentItem: Rectangle { implicitWidth: 3; radius: 2; color: "#929a96" }
                background: null
            }
            onMovementStarted: stickToEnd = false
            onMovementEnded: stickToEnd = atYEnd
            onHeightChanged: { if (stickToEnd) followTimer.restart() }
            // contentHeight is an estimate for variable-height delegates. Following
            // every estimate change can keep repositioning/recreating delegates.
            onCountChanged: { if (stickToEnd) followTimer.restart() }
            Timer { id: followTimer; interval: 16; onTriggered: messageList.keepAtEnd() }
            header: Button {
                width: messageList.width
                implicitHeight: 36
                flat: true
                font.pixelSize: 12
                Material.foreground: Style.secondary
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
                required property var payload
                required property string timeBefore
                readonly property var modelData: payload
                required property int index
                readonly property bool hasTime: Style.showTime(modelData, index > 0 ? {time: timeBefore} : null)
                readonly property bool showSender: !!page.conversation.isGroup && !modelData.isSelf
                readonly property real messageTop: hasTime ? 40 : 0
                width: messageList.width
                height: messageTop + Math.max(40, bubble.height + (showSender ? 23 : 0)) + (delivery.visible ? 24 : 0)
                Label {
                    objectName: "messageTime"
                    visible: row.hasTime
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4
                    text: Style.timeLabel(row.modelData.time)
                    color: "#929292"
                    font.pixelSize: 12
                }
                Avatar {
                    id: avatar
                    backend: page.backend
                    uid: row.modelData.senderUid
                    name: page.senderName(row.modelData)
                    width: 40
                    height: 40
                    y: row.messageTop
                    x: row.modelData.isSelf ? row.width - width - 12 : 12
                }
                Label {
                    visible: row.showSender
                    x: avatar.x + avatar.width + 12
                    y: row.messageTop - 2
                    width: Math.max(0, row.width - 100)
                    text: page.senderName(row.modelData)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Style.secondary
                    font.pixelSize: 12
                }
                MessageBubble {
                    id: bubble
                    objectName: "bubble-" + row.index
                    backend: page.backend
                    message: row.modelData
                    maximumWidth: Math.min(420, Math.max(64, row.width - 112))
                    x: row.modelData.isSelf ? avatar.x - width - 10 : avatar.x + avatar.width + 10
                    y: row.messageTop + (row.showSender ? 23 : 0)
                    onHeightChanged: {
                        if (messageList.stickToEnd)
                            followTimer.restart()
                    }
                    onSaveRequested: function(fileId, name) {
                        saveDialog.fileId = fileId
                        const directory = StandardPaths.writableLocation(StandardPaths.DownloadLocation).replace(/\\/g, "/")
                        const folder = (directory.charAt(0) === "/" ? "file://" : "file:///") + directory
                        saveDialog.selectedFile = folder + "/" + encodeURIComponent(name)
                        saveDialog.open()
                    }
                    onImageRequested: function(fileId) { imagePreview.fileId = fileId; imagePreview.open() }
                }
                Label {
                    id: delivery
                    objectName: "delivery-" + row.index
                    visible: row.modelData.isSelf && row.modelData.msgId === 0
                    x: Math.max(12, bubble.x + bubble.width - width)
                    y: bubble.y + bubble.height + 4
                    text: row.modelData.status === 1 ? qsTr("发送失败 · 点击重试") : qsTr("发送中…")
                    color: row.modelData.status === 1 ? "#c43c35" : Style.secondary
                    font.pixelSize: 12
                    TapHandler {
                        enabled: row.modelData.status === 1 && page.backend.isLoggedIn && !page.backend.sending
                        onTapped: page.backend.retryMessage(page.threadId, row.modelData.uniqueId)
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
            id: composer
            objectName: "composer"
            Layout.fillWidth: true
            padding: 8
            background: Rectangle {
                color: Style.toolbar
                Rectangle { width: parent.width; height: 1; color: "#dadada" }
            }
            RowLayout {
                width: parent.width
                spacing: 6
                ToolButton {
                    id: recordButton
                    implicitWidth: 40; implicitHeight: 44
                    enabled: page.backend.isLoggedIn && page.backend.mediaCapabilities.voice && !page.backend.sending
                        && !page.backend.media.recording && page.backend.media.draftMs === 0 && page.backend.media.call.state === 0
                    contentItem: Item { ChatIcon { anchors.centerIn: parent; name: "voice"; color: recordButton.enabled ? Style.text : "#b5b5b5" } }
                    onClicked: { page.attachmentsVisible = false; Qt.inputMethod.hide(); page.backend.startRecording(page.threadId) }
                    Accessible.name: qsTr("录制语音")
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(120, Math.max(44, input.implicitHeight))
                    contentWidth: availableWidth
                    background: Rectangle { color: Style.surface; radius: 5; border.color: "#e4e4e4" }
                    TextArea {
                        id: input
                        objectName: "messageInput"
                        placeholderText: qsTr("输入消息…")
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        font.pixelSize: 17
                        color: Style.text
                        padding: 10
                        leftPadding: 10
                        rightPadding: 10
                        topPadding: 10
                        bottomPadding: 10
                        background: null
                        onTextChanged: draftTimer.restart()
                        onActiveFocusChanged: if (activeFocus) page.attachmentsVisible = false
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                                page.send()
                                event.accepted = true
                            }
                        }
                    }
                }
                PrimaryButton {
                    objectName: "sendButton"
                    visible: input.text.trim().length > 0
                    text: page.backend.sending ? qsTr("处理中") : qsTr("发送")
                    enabled: input.text.trim().length > 0 && !page.backend.sending && page.backend.isLoggedIn
                    implicitWidth: page.backend.sending ? 76 : 64
                    implicitHeight: 40
                    Material.background: "#07a956"
                    Material.foreground: "white"
                    onClicked: page.send()
                }
                ToolButton {
                    objectName: "attachmentToggle"
                    implicitWidth: 40; implicitHeight: 44
                    contentItem: Item { ChatIcon { anchors.centerIn: parent; name: "plus" } }
                    onClicked: {
                        page.attachmentsVisible = !page.attachmentsVisible
                        if (page.attachmentsVisible) { input.focus = false; Qt.inputMethod.hide() }
                    }
                    Accessible.name: qsTr("更多发送方式")
                }
            }
        }
        Pane {
            objectName: "attachmentPanel"
            visible: page.attachmentsVisible
            Layout.fillWidth: true
            padding: 12
            background: Rectangle { color: Style.toolbar }
            GridLayout {
                width: parent.width
                columns: 4
                Repeater {
                    model: [{label: qsTr("图片"), icon: "image"}, {label: qsTr("文件"), icon: "file"},
                            {label: qsTr("语音通话"), icon: "voice"}, {label: qsTr("视频通话"), icon: "video"}]
                    delegate: ItemDelegate {
                        id: attachmentEntry
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 90
                        enabled: page.backend.isLoggedIn && !page.backend.sending
                            && (modelData.icon === "image" || (modelData.icon === "file" ? page.backend.mediaCapabilities.files
                                : !page.conversation.isGroup && (modelData.icon === "voice" ? page.backend.mediaCapabilities.call : page.backend.mediaCapabilities.video)))
                        contentItem: Column {
                            spacing: 7
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 52; height: 48; radius: 11; color: "white"
                                ChatIcon { anchors.centerIn: parent; name: modelData.icon; color: attachmentEntry.enabled ? Style.text : "#b5b5b5" }
                            }
                            Label { width: parent.width; text: modelData.label; color: Style.secondary; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
                        }
                        onClicked: {
                            page.attachmentsVisible = false
                            if (modelData.icon === "image") imageDialog.open()
                            else if (modelData.icon === "file") fileDialog.open()
                            else page.backend.startCall(page.threadId, modelData.icon === "video")
                        }
                    }
                }
            }
        }
    }
    ToolButton {
        objectName: "latestMessageButton"
        visible: messageList.count > 0 && !messageList.atYEnd
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: Math.max(0, composer.y - height - 8)
        text: "↓"
        implicitWidth: 40
        implicitHeight: 40
        Accessible.name: qsTr("回到最新消息")
        Material.foreground: Style.accent
        background: Rectangle { color: "white"; radius: 20; border.color: Style.separator }
        onClicked: {
            messageList.cancelFlick()
            messageList.stickToEnd = true
            messageList.forceLayout()
            messageList.positionViewAtEnd()
            followTimer.restart()
        }
    }
    FileDialog {
        id: imageDialog
        title: qsTr("选择要发送的图片")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: page.backend.sendImage(page.threadId, selectedFile)
    }
    Popup {
        id: imagePreview
        property string fileId: ""
        parent: Overlay.overlay
        width: page.width
        height: page.height
        anchors.centerIn: parent
        padding: 16
        modal: true
        background: Rectangle { color: "#171717" }
        contentItem: ColumnLayout {
            ToolButton { text: qsTr("关闭预览"); Material.foreground: "white"; onClicked: imagePreview.close() }
            Image {
                Layout.fillWidth: true; Layout.fillHeight: true
                source: imagePreview.opened ? page.backend.imageSource(imagePreview.fileId) : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }
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
            followTimer.restart()
        }
        function onLocalHistoryLoaded(thread, more) { if (thread === page.threadId) page.olderAvailable = more }
        function onContactsChanged() { page.contacts = page.backend.friendList() }
        function onChatThreadsLoaded() { page.conversation = page.backend.threadInfo(page.threadId); page.members = page.backend.groupInfo(page.threadId).members || [] }
    }
}
