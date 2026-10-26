import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Page {
    id: page
    required property var backend
    required property var navigation
    property bool applications: false
    property bool pending: false
    property bool searched: false
    property var result: ({})
    property var requests: []
    property string message: ""
    header: MobileHeader { title: page.applications ? qsTr("新的朋友") : qsTr("添加好友"); onBack: page.navigation.back() }
    function search() {
        if (pending || keyword.text.trim().length === 0) return
        pending = true
        result = ({})
        backend.searchUser(keyword.text)
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        RowLayout {
            visible: !page.applications
            Layout.fillWidth: true
            TextField { id: keyword; Layout.fillWidth: true; implicitHeight: 56; placeholderText: qsTr("用户 ID 或邮箱"); enabled: !page.pending; onAccepted: page.search() }
            Button { text: qsTr("搜索"); enabled: !page.pending; implicitHeight: 56; onClicked: page.search() }
        }
        Label { text: page.message; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.applications ? page.requests : (page.result.uid ? [page.result] : [])
            ScrollBar.vertical: ScrollBar {}
            Label { anchors.centerIn: parent; visible: results.count === 0; text: page.applications ? qsTr("暂无好友申请") : (page.pending ? qsTr("正在搜索…") : (page.searched ? qsTr("没有找到该用户") : qsTr("输入用户 ID 或邮箱查找"))); color: "#667085" }
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                width: results.width
                implicitHeight: details.implicitHeight + 24
                contentItem: ColumnLayout {
                    id: details
                    spacing: 12
                    RowLayout {
                        Avatar { backend: page.backend; uid: entry.modelData.uid; fallback: entry.modelData.icon; name: entry.modelData.displayName }
                        Label { text: entry.modelData.displayName; Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true; textFormat: Text.PlainText }
                    }
                    Label { text: qsTr("用户 ID：%1").arg(entry.modelData.uid); Layout.fillWidth: true }
                    Label { text: entry.modelData.description || ""; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                    Button {
                        text: page.applications ? (entry.modelData.status === 0 ? qsTr("接受申请") : qsTr("已添加")) : (entry.modelData.canAdd ? qsTr("添加为好友") : qsTr("已是好友或本人"))
                        enabled: !page.pending && (page.applications ? entry.modelData.status === 0 : entry.modelData.canAdd)
                        implicitHeight: 52
                        onClicked: {
                            page.pending = true
                            if (page.applications) page.backend.acceptFriendApply(entry.modelData.uid)
                            else page.backend.addFriendApply(entry.modelData.uid, "")
                        }
                    }
                }
            }
        }
    }
    Component.onCompleted: requests = backend.applyList()
    Connections {
        target: page.backend
        function onFriendSearchResult(info) { page.pending = false; page.searched = true; page.result = info }
        function onFriendApplySent(uid) { page.pending = false; page.message = qsTr("好友申请已发送"); page.result = ({}) }
        function onContactsChanged() { page.pending = false; page.requests = page.backend.applyList() }
        function onOperationFailed(message) { page.pending = false; page.message = message }
    }
}
