import QtQuick
import QtQuick.Controls.Material
import "pages"
import "components"

ApplicationWindow {
    id: window
    width: 390
    height: 780
    visible: true
    title: qsTr("CppChat")
    color: "#f5f6f7"
    Material.theme: Material.Light
    Material.accent: "#07a956"
    // Qt >= 6.9 ApplicationWindow applies system safe-area padding itself.
    // adjustResize handles the IME; do not subtract its height a second time.
    header: ToolBar {
        visible: App.reconnecting
        height: visible ? implicitHeight : 0
        Label {
            anchors.fill: parent
            anchors.margins: 8
            text: App.connectionText
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
        implicitHeight: 56
        TapHandler { onTapped: App.reconnectNow() }
    }

    QtObject {
        id: navigator
        objectName: "navigation"
        function push(name, properties) {
            if (stack.busy) return
            Qt.inputMethod.hide()
            const pages = { register: registerPage, reset: resetPage, settings: settingsPage,
                            search: searchPage, applications: applicationsPage, detail: detailPage,
                            history: historyPage, group: groupPage }
            stack.push(pages[name], properties || {})
        }
        function back() {
            if (stack.busy) return
            Qt.inputMethod.hide()
            if (stack.depth > 1) stack.pop()
        }
        function root(page, properties) {
            Qt.inputMethod.hide()
            stack.clear(StackView.Immediate)
            stack.push(page, properties || {}, StackView.Immediate)
        }
    }

    StackView {
        id: stack
        anchors.fill: parent
        focus: true
        initialItem: loginPage
        Keys.onReleased: function(event) {
            if (event.key !== Qt.Key_Back && event.key !== Qt.Key_Escape) return
            if (App.media.call.state !== 0) {
                App.media.endCall()
                event.accepted = true
                return
            }
            if (Qt.inputMethod.visible) {
                Qt.inputMethod.hide()
                event.accepted = true
            } else if (stack.depth > 1) {
                navigator.back()
                event.accepted = true
            }
        }
    }
    CallPanel { backend: App }
    onClosing: function(close) {
        if (App.media.call.state !== 0) {
            close.accepted = false
            App.media.endCall()
            return
        }
        if (stack.depth > 1 || Qt.inputMethod.visible) {
            close.accepted = false
            if (Qt.inputMethod.visible) Qt.inputMethod.hide()
            else navigator.back()
        } else if (App.sessionActive && App.backgroundApplication()) {
            close.accepted = false
        }
    }

    Component { id: loginPage; LoginPage { backend: App; navigation: navigator } }
    Component { id: registerPage; RegisterPage { backend: App; navigation: navigator } }
    Component { id: resetPage; ResetPage { backend: App; navigation: navigator } }
    Component { id: chatListPage; ChatListPage { backend: App; navigation: navigator } }
    Component { id: detailPage; ChatDetailPage { backend: App; navigation: navigator } }
    Component { id: settingsPage; SettingsPage { backend: App; navigation: navigator } }
    Component { id: historyPage; HistoryPage { backend: App; navigation: navigator } }
    Component { id: groupPage; GroupPage { backend: App; navigation: navigator } }
    Component { id: searchPage; FriendPage { backend: App; navigation: navigator } }
    Component { id: applicationsPage; FriendPage { backend: App; navigation: navigator; applications: true } }

    Popup {
        id: toast
        property string message: ""
        x: Math.max(12, (window.width - width) / 2)
        y: Math.max(12, window.contentItem.height - height - 24)
        width: Math.min(440, window.width - 24)
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentItem: Label { text: toast.message; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        Timer { interval: 4500; running: toast.opened; onTriggered: toast.close() }
    }
    Connections {
        target: App
        function onChatSwitched() { navigator.root(chatListPage) }
        function onForceLogout(reason) { navigator.root(loginPage, {message: reason}) }
        function onChatOpened(threadId, name) {
            navigator.push("detail", {threadId: threadId, title: name})
        }
        function onOperationFailed(message) { toast.message = message; toast.open() }
        function onStatusMessage(message) { toast.message = message; toast.open() }
    }
    Component.onCompleted: App.startAutomaticLogin()
}
