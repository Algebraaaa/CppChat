import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"
Page {
    id: page
    required property var backend
    required property var navigation
    property int threadId: 0
    property var results: []
    property bool more: false
    property int nextId: 2147483647
    property double nextLocal: 9007199254740991
    property bool append: false
    header: MobileHeader { title: qsTr("本地聊天记录"); onBack: page.navigation.back() }
    function search(older) {
        append = older
        backend.searchHistory(threadId, query.text, kind.currentIndex - 1,
                              older ? nextId : 2147483647, older ? nextLocal : 9007199254740991)
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12
        TextField { id: query; Layout.fillWidth: true; placeholderText: qsTr("搜索内容，留空显示全部"); onAccepted: page.search(false) }
        RowLayout {
            ComboBox { id: kind; model: [qsTr("全部"), qsTr("文字"), qsTr("图片")] }
            Button { text: qsTr("搜索"); onClicked: page.search(false) }
            Button { text: qsTr("清空"); onClicked: clear.open() }
        }
        ListView {
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: page.results
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                required property var modelData
                width: list.width
                contentItem: ColumnLayout {
                    Label { text: modelData.time + "  ·  " + modelData.senderUid; color: "#667085"; font.pixelSize: 12 }
                    Label { text: modelData.type === 1 ? qsTr("[图片]") : modelData.content; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                }
            }
            footer: Button { visible: page.more; text: qsTr("加载更早记录"); width: list.width; onClicked: page.search(true) }
            Label { visible: list.count === 0; anchors.centerIn: parent; text: qsTr("没有匹配的本地记录") }
        }
    }
    Dialog {
        id: clear; anchors.centerIn: Overlay.overlay; width: Math.min(340, page.width - 24); implicitHeight: 260
        title: qsTr("清空本地记录"); modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: qsTr("清空此会话已确认的本机消息，保留未发送消息。服务器与其他设备的记录不变，本机不会再次显示已清空的旧消息。"); wrapMode: Text.Wrap }
        onAccepted: page.backend.clearLocalHistory(page.threadId)
    }
    Component.onCompleted: search(false)
    Connections {
        target: page.backend
        function onHistoryResults(thread, messages, hasMore, cursor, local) {
            if (thread !== page.threadId) return
            page.results = page.append ? page.results.concat(messages) : messages
            page.more = hasMore; page.nextId = cursor; page.nextLocal = local
        }
        function onHistoryCleared(thread) { if (thread === page.threadId) page.search(false) }
    }
}
