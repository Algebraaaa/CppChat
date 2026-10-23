import QtQuick
import QtQuick.Controls.Material

Button {
    id: root
    property double deadline: 0
    property int seconds: 0
    text: seconds > 0 ? qsTr("%1 秒").arg(seconds) : qsTr("获取验证码")
    enabled: seconds === 0
    implicitHeight: 52
    function startTimer() { deadline = Date.now() + 60000; updateSeconds() }
    function updateSeconds() { seconds = Math.max(0, Math.ceil((deadline - Date.now()) / 1000)) }
    Timer { interval: 500; repeat: true; running: root.seconds > 0; onTriggered: root.updateSeconds() }
}
