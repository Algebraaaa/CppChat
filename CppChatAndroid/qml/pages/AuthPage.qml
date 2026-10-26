import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Page {
    id: page
    required property var backend
    required property var navigation
    property string mode: "login"
    property string message: ""
    readonly property bool login: mode === "login"
    readonly property string actionText: login ? qsTr("登录") : (mode === "register" ? qsTr("注册账号") : qsTr("重置密码"))
    title: actionText
    background: Rectangle { color: "#ffffff" }
    header: MobileHeader { visible: !page.login; title: page.actionText; onBack: page.navigation.back() }

    function submit() {
        if (backend.authBusy) return
        message = ""
        Qt.inputMethod.commit()
        if (login) backend.loginUser(email.text, password.text)
        else if (mode === "register") backend.registerUser(email.text, password.text, confirm.text, code.text, user.text)
        else backend.resetPassword(email.text, password.text, confirm.text, code.text, user.text)
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: form.height + 40
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        function reveal(item) {
            if (!item || !item.activeFocus) return
            const point = item.mapToItem(contentItem, 0, 0)
            if (point.y < contentY + 12) contentY = Math.max(0, point.y - 12)
            else if (point.y + item.height > contentY + height - 16)
                contentY = Math.min(Math.max(0, contentHeight - height), point.y + item.height - height + 16)
        }
        onHeightChanged: Qt.callLater(function() { scroll.reveal(page.ApplicationWindow.window.activeFocusControl) })
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: form
            width: Math.max(0, Math.min(440, scroll.width - 40))
            x: (scroll.width - width) / 2
            y: 20
            spacing: 12
            Item { Layout.preferredHeight: page.login && scroll.height > 600 ? 48 : 0 }
            Label { text: "CppChat"; font.pixelSize: 30; font.bold: true; color: "#07a956"; Layout.alignment: Qt.AlignHCenter }
            Label { text: page.login ? qsTr("随时保持联系") : page.actionText; Layout.alignment: Qt.AlignHCenter; color: "#667085"; Layout.bottomMargin: 16 }
            TextField {
                id: user
                visible: !page.login
                Layout.fillWidth: true
                implicitHeight: 56
                placeholderText: qsTr("用户名")
                maximumLength: 20
                enabled: !page.backend.authBusy
                onActiveFocusChanged: scroll.reveal(user)
                onAccepted: email.forceActiveFocus()
            }
            TextField {
                id: email
                Layout.fillWidth: true
                implicitHeight: 56
                placeholderText: qsTr("邮箱")
                text: page.login ? page.backend.account.email : ""
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                enabled: !page.backend.authBusy
                onActiveFocusChanged: scroll.reveal(email)
                onAccepted: password.forceActiveFocus()
            }
            TextField {
                id: password
                Layout.fillWidth: true
                implicitHeight: 56
                placeholderText: page.mode === "reset" ? qsTr("新密码（8～32 位字母和数字）") : qsTr("密码")
                echoMode: TextInput.Password
                text: page.login ? page.backend.account.password : ""
                maximumLength: 32
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                enabled: !page.backend.authBusy
                onActiveFocusChanged: scroll.reveal(password)
                onAccepted: { if (page.login) page.submit(); else confirm.forceActiveFocus() }
            }
            TextField {
                id: confirm
                visible: !page.login
                Layout.fillWidth: true
                implicitHeight: 56
                placeholderText: qsTr("确认密码")
                echoMode: TextInput.Password
                maximumLength: 32
                enabled: !page.backend.authBusy
                onActiveFocusChanged: scroll.reveal(confirm)
                onAccepted: code.forceActiveFocus()
            }
            RowLayout {
                visible: !page.login
                Layout.fillWidth: true
                TextField {
                    id: code
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    implicitHeight: 56
                    placeholderText: qsTr("6 位验证码")
                    maximumLength: 6
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: !page.backend.authBusy
                    onActiveFocusChanged: scroll.reveal(code)
                    onAccepted: page.submit()
                }
                TimerButton {
                    id: codeButton
                    Layout.preferredWidth: 124
                    enabled: seconds === 0 && !page.backend.authBusy
                    onClicked: page.backend.requestVerifyCode(email.text)
                }
            }
            Label {
                Layout.fillWidth: true
                visible: page.message.length > 0
                text: page.message
                textFormat: Text.PlainText
                color: "#b42318"
                wrapMode: Text.Wrap
            }
            PrimaryButton {
                Layout.fillWidth: true
                implicitHeight: 56
                text: page.backend.authBusy ? qsTr("正在处理…") : page.actionText
                enabled: !page.backend.authBusy
                Material.background: "#07a956"
                Material.foreground: "white"
                onClicked: page.submit()
            }
            RowLayout {
                visible: page.login
                Layout.fillWidth: true
                Button { text: qsTr("注册账号"); flat: true; enabled: !page.backend.authBusy; onClicked: page.navigation.push("register") }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("忘记密码"); flat: true; enabled: !page.backend.authBusy; onClicked: page.navigation.push("reset") }
            }
            RowLayout {
                visible: page.login
                Layout.fillWidth: true
                CheckBox { text: qsTr("记住密码"); checked: page.backend.account.remember; enabled: !page.backend.authBusy; onToggled: page.backend.account.remember = checked }
                CheckBox { text: qsTr("自动登录"); checked: page.backend.account.automatic; enabled: !page.backend.authBusy; onToggled: page.backend.account.automatic = checked }
            }
            Label { visible: page.login; text: qsTr("密码由系统安全存储保护，仅在此设备使用"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#667085"; font.pixelSize: 12 }
        }
    }
    Connections {
        target: page.backend
        function onLoginHttpResult(error, message) { if (page.login) page.message = message }
        function onVerifyCodeResult(error, message) {
            if (page.StackView.status !== StackView.Active || page.login) return
            page.message = message
            if (error === 0) codeButton.startTimer()
        }
        function onRegisterResult(error, message) {
            if (page.mode !== "register") return
            if (error === 0) page.navigation.back()
            else page.message = message
        }
        function onResetResult(error, message) {
            if (page.mode !== "reset") return
            if (error === 0) page.navigation.back()
            else page.message = message
        }
    }
}
