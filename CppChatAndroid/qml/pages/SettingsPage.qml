import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"
Page {
    id: page
    implicitWidth: 360
    required property var backend
    required property var navigation
    property var storage: backend.storageInfo()
    readonly property var update: backend.updateInfo
    header: MobileHeader { title: qsTr("账号、数据与更新"); onBack: page.navigation.back() }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: Math.max(0, Math.min(560, page.width - 32))
            x: (page.width - width) / 2
            spacing: 12
            Label { text: qsTr("个人资料"); font.bold: true; font.pixelSize: 20; Layout.topMargin: 20 }
            TextField { id: nick; text: page.backend.currentNick; placeholderText: qsTr("昵称"); maximumLength: 32; Layout.fillWidth: true }
            TextField { id: description; text: page.backend.currentDescription; placeholderText: qsTr("签名（选填）"); maximumLength: 200; Layout.fillWidth: true }
            Button { text: qsTr("保存昵称和签名"); onClicked: page.backend.updateProfile(nick.text, description.text) }
            Label { text: qsTr("登录设置"); font.bold: true; font.pixelSize: 20 }
            CheckBox { text: qsTr("记住密码"); checked: page.backend.account.remember; onToggled: page.backend.account.remember = checked }
            CheckBox { text: qsTr("下次自动登录"); checked: page.backend.account.automatic; onToggled: page.backend.account.automatic = checked }
            Button { text: qsTr("清除记住的账号密码"); onClicked: page.backend.account.forget() }
            Label { text: qsTr("本地聊天记录"); font.bold: true; font.pixelSize: 20 }
            Label { text: page.storage.description; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("当前账号占用：%1 MB").arg((page.storage.bytes / 1048576).toFixed(2)); Layout.fillWidth: true }
            Label { text: page.storage.database ? qsTr("数据库：\n%1").arg(page.storage.database) : qsTr("登录后显示当前账号实际存储路径"); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 12; color: "#667085"; textFormat: Text.PlainText }
            Label { text: qsTr("在聊天页右上角可搜索或清空该会话的本地记录。清空不会删除服务器或其他设备的记录。"); Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("版本与更新"); font.bold: true; font.pixelSize: 20 }
            Label { text: qsTr("当前版本：%1").arg(page.update.current) }
            Label { text: page.update.message || qsTr("启动后及前台运行期间定期检查更新"); Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { visible: !!page.update.version; text: qsTr("可用版本：%1\n%2").arg(page.update.version).arg(page.update.notes); Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            ProgressBar { visible: page.update.state === 3; Layout.fillWidth: true; indeterminate: page.update.total <= 0; value: page.update.total > 0 ? page.update.received / page.update.total : 0 }
            Label {
                visible: page.update.state === 3 || (!page.update.background && page.update.state === 2 && page.update.received > 0)
                Layout.fillWidth: true
                text: qsTr("%1 / %2 MB").arg((page.update.received / 1048576).toFixed(1)).arg((page.update.total / 1048576).toFixed(1))
                    + (page.update.state === 3 ? qsTr(" · %1 KiB/s").arg((page.update.bytesPerSecond / 1024).toFixed(0)) : "")
            }
            RowLayout {
                Button { objectName: "updateCheckButton"; text: qsTr("检查更新"); enabled: !page.update.verifying && page.update.state !== 1 && page.update.state !== 3; onClicked: page.backend.checkUpdates() }
                Button { objectName: "updateDownloadButton"; visible: page.update.state === 2; text: !page.update.background && page.update.received > 0 ? qsTr("继续下载") : qsTr("下载更新"); onClicked: page.backend.downloadUpdate() }
                Button { objectName: "updateCancelButton"; visible: page.update.state === 3; enabled: !page.update.verifying; text: page.update.background ? qsTr("取消下载") : qsTr("暂停下载"); onClicked: page.backend.cancelUpdate() }
                Button { objectName: "updateInstallButton"; visible: page.update.state === 4; enabled: !page.update.verifying; text: qsTr("安装更新"); onClicked: page.backend.installUpdate() }
            }
            Label { text: page.update.background ? qsTr("更新交给安卓系统下载，可锁屏、切换应用或正常退出。重新打开会接回任务，网络或系统限制可能暂缓下载。校验大小、SHA-256、包名和签名后由系统确认安装；取消安装可稍后重试。强行停止、清除数据及厂商省电限制可能影响任务。") : qsTr("下载后校验大小与 SHA-256，由安卓系统确认安装。应用内更新需允许此应用安装未知来源应用；覆盖更新会保留本地记录。"); Layout.fillWidth: true; wrapMode: Text.Wrap; Layout.bottomMargin: 24 }
        }
    }
}
