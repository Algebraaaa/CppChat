import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"
import "../components/ChatStyle.js" as Style

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
    background: Rectangle { color: Style.background }
    header: ToolBar {
        implicitHeight: 56
        Material.background: Style.toolbar
        Material.foreground: Style.text
        background: Rectangle {
            color: Style.toolbar
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Style.separator }
        }
        RowLayout {
            anchors.fill: parent
            Label { text: tabs.currentIndex === 0 ? "CppChat" : tabs.currentIndex === 1 ? qsTr("通讯录") : qsTr("我"); color: Style.text; font.pixelSize: 21; font.weight: Font.DemiBold; Layout.leftMargin: 18; Layout.fillWidth: true }
            ToolButton { implicitWidth: 48; implicitHeight: 56; contentItem: Item { ChatIcon { anchors.centerIn: parent; name: "search" } } visible: tabs.currentIndex < 2; onClicked: search.forceActiveFocus(); Accessible.name: qsTr("搜索") }
            ToolButton { implicitWidth: 48; implicitHeight: 56; contentItem: Item { ChatIcon { anchors.centerIn: parent; name: "plus" } } onClicked: addMenu.open(); Accessible.name: qsTr("添加好友或发起群聊") }
        }
    }
    footer: TabBar {
        id: tabs
        objectName: "mainTabs"
        implicitHeight: 64
        Material.accent: "transparent"
        background: Rectangle { color: Style.toolbar; Rectangle { width: parent.width; height: 1; color: Style.separator } }
        Repeater {
            model: [{label: qsTr("聊天"), icon: "chat"}, {label: qsTr("通讯录"), icon: "contacts"}, {label: qsTr("我"), icon: "user"}]
            TabButton {
                id: tab
                required property var modelData
                implicitHeight: 64
                topPadding: 9; bottomPadding: 7
                Accessible.name: modelData.label
                background: Rectangle { color: tab.down ? "#e8e8e8" : "transparent" }
                contentItem: Column {
                    spacing: 3
                    ChatIcon { anchors.horizontalCenter: parent.horizontalCenter; name: tab.modelData.icon; color: tab.checked ? Style.accent : Style.text }
                    Label { width: parent.width; text: tab.modelData.label; font.pixelSize: 11; color: tab.checked ? Style.accent : Style.text; horizontalAlignment: Text.AlignHCenter }
                }
            }
        }
        onCurrentIndexChanged: { search.text = ""; Qt.inputMethod.hide() }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TextField {
            id: search
            objectName: "searchField"
            visible: tabs.currentIndex < 2
            Layout.fillWidth: true
            Layout.margins: 8
            implicitHeight: 38
            leftPadding: 38
            font.pixelSize: 15
            color: Style.text
            background: Rectangle { color: Style.surface; radius: 7 }
            ChatIcon { name: "search"; color: "#a5a5a5"; x: 12; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18 }
            placeholderText: tabs.currentIndex === 0 ? qsTr("搜索会话") : qsTr("搜索联系人")
            inputMethodHints: Qt.ImhNoAutoUppercase
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: conversations
                objectName: "conversationList"
                clip: true
                model: page.filteredThreads
                ScrollBar.vertical: ScrollBar {}
                Label {
                    anchors.centerIn: parent
                    visible: conversations.count === 0
                    text: search.text ? qsTr("没有匹配的会话") : qsTr("还没有聊天\n去通讯录找朋友聊聊吧")
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.7
                    color: Style.secondary
                }
                delegate: ItemDelegate {
                    id: conversation
                    required property var modelData
                    width: conversations.width
                    implicitHeight: 76
                    leftPadding: 16; rightPadding: 16
                    background: Rectangle {
                        color: conversation.down ? "#dedede" : conversation.modelData.pinned ? "#f4f4f4" : Style.surface
                        Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: Math.max(0, parent.width - 76); height: 1; color: "#eeeeee" }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { backend: page.backend; uid: conversation.modelData.otherId; fallback: conversation.modelData.icon; name: conversation.modelData.name }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Label { text: conversation.modelData.name; color: Style.text; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 17; textFormat: Text.PlainText }
                            Label { text: conversation.modelData.lastMsg; Layout.fillWidth: true; elide: Text.ElideRight; color: "#999999"; font.pixelSize: 13; textFormat: Text.PlainText }
                        }
                        Label { text: Style.listTime(conversation.modelData.lastTime); color: "#aaaaaa"; font.pixelSize: 11; Layout.alignment: Qt.AlignTop; Layout.topMargin: 8 }
                    }
                    onClicked: page.navigation.push("detail", {threadId: modelData.threadId, title: modelData.name})
                    onPressAndHold: { conversationMenu.thread = modelData; conversationMenu.popup() }
                }
            }
            ListView {
                id: contactList
                objectName: "contactList"
                clip: true
                model: page.filteredContacts
                ScrollBar.vertical: ScrollBar {}
                header: ItemDelegate {
                    width: contactList.width
                    implicitHeight: 68
                    leftPadding: 16; rightPadding: 16
                    background: Rectangle { color: Style.surface }
                    contentItem: RowLayout {
                        spacing: 12
                        Rectangle { Layout.preferredWidth: 42; Layout.preferredHeight: 42; radius: 6; color: "#fa9d3b"; ChatIcon { anchors.centerIn: parent; name: "contacts"; color: "white" } }
                        Label { text: qsTr("新的朋友"); Layout.fillWidth: true; color: Style.text; font.pixelSize: 16 }
                        ChatIcon { name: "chevron"; color: "#b5b5b5"; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                    }
                    onClicked: page.navigation.push("applications")
                }
                delegate: ItemDelegate {
                    id: contact
                    required property var modelData
                    width: contactList.width
                    implicitHeight: 68
                    leftPadding: 16; rightPadding: 16
                    background: Rectangle {
                        color: contact.down ? "#dedede" : Style.surface
                        Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: Math.max(0, parent.width - 76); height: 1; color: "#eeeeee" }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { backend: page.backend; uid: contact.modelData.uid; fallback: contact.modelData.icon; name: contact.modelData.displayName; Layout.preferredWidth: 42; Layout.preferredHeight: 42 }
                        Label { text: contact.modelData.displayName; color: Style.text; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 17; textFormat: Text.PlainText }
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
    Menu {
        id: addMenu
        x: Math.max(0, page.width - width - 8)
        MenuItem { text: qsTr("添加好友"); onTriggered: page.navigation.push("search") }
        MenuItem { text: qsTr("发起群聊"); onTriggered: page.navigation.push("group", {threadId: 0}) }
        MenuItem { text: qsTr("刷新聊天记录"); onTriggered: page.backend.loadChatThreads() }
    }
    Menu {
        id: conversationMenu
        property var thread: ({})
        MenuItem { text: conversationMenu.thread.pinned ? qsTr("取消置顶") : qsTr("置顶聊天"); onTriggered: page.backend.setPinned(conversationMenu.thread.threadId, !conversationMenu.thread.pinned) }
        MenuItem { text: qsTr("搜索聊天记录"); onTriggered: page.navigation.push("history", {threadId: conversationMenu.thread.threadId}) }
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
