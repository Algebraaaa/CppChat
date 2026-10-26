import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"
Page {
    id: page
    implicitWidth: 360
    required property var backend
    required property var navigation
    property int threadId: 0
    property var group: backend.groupInfo(threadId)
    property var friends: backend.friendList()
    property var selected: []
    property bool busy: false
    property string pending: ""
    property string creationKey: backend.newGroupCreationKey()
    readonly property bool owner: group.owner_uid === backend.currentUid()
    header: MobileHeader { title: page.threadId ? qsTr("群聊管理") : qsTr("发起群聊"); onBack: page.navigation.back() }
    function send(op, fields) {
        if (busy) return
        fields = fields || {}
        if (op === "create") fields.creation_key = creationKey
        pending = op; busy = true; backend.groupCommand(op, threadId, fields)
    }
    ScrollView {
        anchors.fill: parent; contentWidth: availableWidth
        ColumnLayout {
            width: Math.max(0, page.width - 32); x: 16; spacing: 10
            Label { text: page.group.name || qsTr("选择好友组建群聊"); font.pixelSize: 20; Layout.topMargin: 16; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            TextField { id: name; text: page.group.name || ""; maximumLength: 64; placeholderText: qsTr("群名称"); Layout.fillWidth: true }
            Button { text: page.threadId ? qsTr("保存群名") : qsTr("创建群聊"); enabled: !page.busy && (!page.threadId || page.owner); onClicked: page.send(page.threadId ? "rename" : "create", {name: name.text, members: page.selected}) }
            Label { text: page.busy ? qsTr("正在同步…") : qsTr("好友列表"); font.bold: true }
            Repeater {
                model: page.friends
                CheckBox {
                    required property var modelData
                    text: modelData.displayName; Layout.fillWidth: true
                    onToggled: { let ids = page.selected.slice(); if (checked) ids.push(modelData.uid); else ids = ids.filter(function(id) { return id !== modelData.uid }); page.selected = ids }
                }
            }
            Button { visible: page.threadId > 0; text: qsTr("邀请选中的好友"); enabled: !page.busy && page.selected.length === 1; onClicked: page.send("add", {member_uid: page.selected[0]}) }
            Label { visible: page.threadId > 0; text: qsTr("邀请时每次选择一位好友"); color: "#667085" }
            Label { visible: page.threadId > 0; text: qsTr("群成员"); font.bold: true }
            Repeater {
                model: page.group.members || []
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: (modelData.nick || modelData.name || modelData.uid) + (modelData.uid === page.group.owner_uid ? qsTr(" · 群主") : ""); textFormat: Text.PlainText }
                    RowLayout {
                        visible: page.owner && modelData.uid !== page.backend.currentUid()
                        Button { text: qsTr("移出群聊"); enabled: !page.busy; onClicked: { confirmation.operation = "remove"; confirmation.fields = {member_uid: modelData.uid}; confirmation.open() } }
                        Button { text: qsTr("转让群主"); enabled: !page.busy; onClicked: { confirmation.operation = "transfer"; confirmation.fields = {member_uid: modelData.uid}; confirmation.open() } }
                    }
                }
            }
            Button { visible: page.threadId > 0; text: page.owner ? qsTr("解散群聊") : qsTr("退出群聊"); enabled: !page.busy; Layout.bottomMargin: 24; onClicked: { confirmation.operation = page.owner ? "disband" : "leave"; confirmation.fields = {}; confirmation.open() } }
        }
    }
    Dialog {
        id: confirmation; property string operation; property var fields: ({})
        anchors.centerIn: Overlay.overlay; width: Math.min(340, page.width - 24); implicitHeight: 220
        modal: true; title: qsTr("确认群管理操作"); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: qsTr("该操作会同步到服务器并影响群成员，确定继续？"); wrapMode: Text.Wrap }
        onAccepted: page.send(operation, fields)
    }
    Component.onCompleted: { if (threadId) send("info") }
    Connections {
        target: page.backend
        function onGroupResult(response) {
            if (!page.busy || response.op !== page.pending) return
            page.busy = false
            if (response.error !== 0) return
            if (response.group) { page.group = response.group; page.threadId = response.group.thread_id }
            if (["leave", "disband", "create"].indexOf(response.op) >= 0) page.navigation.back()
        }
        function onOperationFailed(message) { page.busy = false }
    }
}
