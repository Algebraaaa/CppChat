import QtQuick
import QtQuick.Controls.Material
import QtTest
import "../../../CppChatAndroid/qml/pages"

Item {
    width: 390
    height: 1800
    QtObject {
        id: backend
        property string currentNick: "test"
        property string currentDescription: ""
        property int cancellations: 0
        property int installations: 0
        property QtObject account: QtObject {
            property bool remember: false
            property bool automatic: false
            function forget() {}
        }
        property var updateInfo: ({})
        function storageInfo() { return {description: "test", bytes: 0, database: ""} }
        function updateProfile() {}
        function checkUpdates() {}
        function downloadUpdate() {}
        function cancelUpdate() { cancellations++ }
        function installUpdate() { installations++ }
    }
    QtObject { id: navigation; function back() {} }
    SettingsPage { id: page; anchors.fill: parent; backend: backend; navigation: navigation }
    TestCase {
        name: "MobileUpdateUi"
        when: windowShown
        function offer(state, background, verifying, received) {
            backend.updateInfo = {current: "0.1.24", version: "0.1.25", notes: "test",
                message: "", state: state, background: background, verifying: verifying,
                received: received, total: 1000, bytesPerSecond: 100}
            wait(0)
        }
        function button(name) { return findChild(page, name) }
        function test_systemTransferOffersCancellation() {
            offer(3, true, false, 300)
            const cancel = button("updateCancelButton")
            compare(cancel.text, "取消下载")
            verify(cancel.enabled)
            const before = backend.cancellations
            cancel.clicked()
            compare(backend.cancellations, before + 1)
        }
        function test_verificationDisablesCompetingActions() {
            offer(3, true, true, 1000)
            verify(!button("updateCheckButton").enabled)
            verify(!button("updateCancelButton").enabled)
            offer(4, true, true, 1000)
            verify(!button("updateInstallButton").enabled)
        }
        function test_readyPackageRemainsInstallable() {
            offer(4, true, false, 1000)
            const install = button("updateInstallButton")
            verify(install.visible)
            verify(install.enabled)
            const before = backend.installations
            install.clicked()
            compare(backend.installations, before + 1)
            verify(install.visible)
        }
        function test_retryIsNotLabelledResumeForSystemFailedTask() {
            offer(2, true, false, 300)
            compare(button("updateDownloadButton").text, "下载更新")
            offer(2, false, false, 300)
            compare(button("updateDownloadButton").text, "继续下载")
            offer(3, false, false, 300)
            compare(button("updateCancelButton").text, "暂停下载")
        }
    }
}
