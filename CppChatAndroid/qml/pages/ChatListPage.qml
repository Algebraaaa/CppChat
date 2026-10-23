import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Page {
    id: page
    required property var backend
    required property var navigation
    property var threads: []
    property var contacts: []
    readonly property var filteredThreads: threads.filter(function(t) {
        return (t.name + " " + t.lastMsg).toLowerCase().indexOf(search.text.toLowerCase()) >= 0
    })
    readonly property var filteredContacts: contacts.filter(function(t) {
        return (t.displayName + " " + t.name).toLowerCase().indexOf(search.text.toLowerCase()) >= 0
    })
    function refresh() { threads = backend.chatThreads(); contacts = backend.friendList() }
    background: Rectangle { color: "#ffffff" }
    header: ToolBar {
        Material.background: "#07a956"
        Material.foreground: "white"
        background: Rectangle { color: "#07a956" }
        RowLayout {
            anchors.fill: parent
            Label { text: "CppChat"; font.pixelSize: 21; font.bold: true; Layout.leftMargin: 20; Layout.fillWidth: true }
            ToolButton { text: qsTr("添加好友"); implicitHeight: 56; onClicked: page.navigation.push("search") }
            ToolButton { text: qsTr("建群"); implicitHeight: 56; onClicked: page.navigation.push("group", {threadId: 0}) }
        }
    }
    footer: TabBar {
        id: tabs
        TabButton { text: qsTr("聊天"); implicitHeight: 56 }
        TabButton { text: qsTr("通讯录"); implicitHeight: 56 }
        TabButton { text: qsTr("我"); implicitHeight: 56 }
        onCurrentIndexChanged: { search.text = ""; Qt.inputMethod.hide() }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TextField {
            id: search
            visible: tabs.currentIndex < 2
            Layout.fillWidth: true
            Layout.margins: 12
            implicitHeight: 52
            placeholderText: tabs.currentIndex === 0 ? qsTr("搜索会话") : qsTr("搜索联系人")
            inputMethodHints: Qt.ImhNoAutoUppercase
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: conversations
                clip: true
                model: page.filteredThreads
                ScrollBar.vertical: ScrollBar {}
                header: Button {
                    text: qsTr("刷新聊天记录")
                    flat: true
                    width: conversations.width
                    onClicked: page.backend.loadChatThreads()
                }
                Label {
                    anchors.centerIn: parent
                    visible: conversations.count === 0
                    text: search.text ? qsTr("没有匹配的会话") : qsTr("暂无聊天，可从通讯录发起")
                    color: "#667085"
                }
                delegate: ItemDelegate {
                    id: conversation
                    required property var modelData
                    width: conversations.width
                    implicitHeight: 84
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { backend: page.backend; uid: conversation.modelData.otherId; fallback: conversation.modelData.icon; name: conversation.modelData.name }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: (conversation.modelData.pinned ? "↑ " : "") + conversation.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 17; textFormat: Text.PlainText }
                            Label { text: conversation.modelData.lastMsg; Layout.fillWidth: true; elide: Text.ElideRight; color: "#667085"; textFormat: Text.PlainText }
                        }
                        Label { text: conversation.modelData.lastTime.slice(5, 16); color: "#98a2b3"; font.pixelSize: 11; Layout.alignment: Qt.AlignTop }
                    }
                    onClicked: page.navigation.push("detail", {threadId: modelData.threadId, title: modelData.name})
                }
            }
            ListView {
                id: contactList
                clip: true
                model: page.filteredContacts
                ScrollBar.vertical: ScrollBar {}
                header: Button { text: qsTr("新的朋友 / 好友申请"); width: contactList.width; implicitHeight: 56; onClicked: page.navigation.push("applications") }
                delegate: ItemDelegate {
                    id: contact
                    required property var modelData
                    width: contactList.width
                    implicitHeight: 76
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { backend: page.backend; uid: contact.modelData.uid; fallback: contact.modelData.icon; name: contact.modelData.displayName }
                        Label { text: contact.modelData.displayName; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 17; textFormat: Text.PlainText }
                    }
                    onClicked: page.backend.openChat(modelData.uid)
                }
                Label { anchors.centerIn: parent; visible: contactList.count === 0; text: qsTr("暂无联系人"); color: "#667085" }
            }
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Avatar { backend: page.backend; uid: page.backend.currentUid(); name: page.backend.currentNick || page.backend.currentUser; Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 32 }
                    Label { text: page.backend.currentNick || page.backend.currentUser; Layout.alignment: Qt.AlignHCenter; font.pixelSize: 22; textFormat: Text.PlainText }
                    Label { text: qsTr("用户 ID：%1").arg(page.backend.currentUid()); Layout.alignment: Qt.AlignHCenter; color: "#667085" }
                    Button { text: qsTr("账号、数据与更新"); Layout.alignment: Qt.AlignHCenter; implicitHeight: 56; onClicked: page.navigation.push("settings") }
                    Button { text: qsTr("退出登录"); Layout.alignment: Qt.AlignHCenter; implicitHeight: 56; onClicked: logout.open() }
                }
            }
        }
    }
    Dialog {
        id: logout
        anchors.centerIn: Overlay.overlay
        width: Math.max(240, Math.min(340, page.width - 32))
        implicitHeight: 240
        title: qsTr("退出登录")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: qsTr("退出后可以使用其他账号登录，本机聊天记录会保留。"); wrapMode: Text.Wrap }
        onAccepted: page.backend.disconnectChat()
    }
    Component.onCompleted: refresh()
    Connections {
        target: page.backend
        function onChatThreadsLoaded() { page.refresh() }
        function onContactsChanged() { page.refresh() }
    }
}
