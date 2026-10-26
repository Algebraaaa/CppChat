import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia

Popup {
    id: panel
    required property var backend
    property var media: backend.media
    property var call: media.call
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? parent.width : 390
    height: parent ? parent.height : 780
    visible: call.state !== 0
    modal: true
    z: 200
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 16
    background: Rectangle { color: "#18242a" }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: panel.call.video ? qsTr("视频通话") : qsTr("语音通话"); color: "#c8d6dd"; Layout.alignment: Qt.AlignHCenter }
        Label { text: panel.call.name; color: "white"; font.pixelSize: 24; textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
        Label {
            objectName: "callStatus"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#c8d6dd"
            text: panel.media.permissionPending ? qsTr("等待系统权限授权…")
                : panel.call.state === 1 ? qsTr("等待对方接听…")
                : panel.call.state === 2 ? qsTr("邀请你通话")
                : panel.call.state === 3 ? qsTr("正在连接…")
                : panel.call.recovering ? qsTr("网络波动，正在恢复…")
                : qsTr("通话中 %1:%2").arg(Math.floor(panel.call.seconds / 60)).arg((panel.call.seconds % 60).toString().padStart(2, "0"))
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
            color: "#10191e"
            radius: 12
            VideoOutput { id: remoteVideo; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit; visible: panel.call.video && panel.call.state === 4 && panel.call.peerCamera }
            Label {
                anchors.centerIn: parent
                color: "#c8d6dd"
                text: panel.call.video ? (panel.call.peerCamera ? qsTr("等待对方画面") : qsTr("对方已关闭摄像头")) : "♪"
                font.pixelSize: panel.call.video ? 14 : 52
                visible: !panel.call.video || panel.call.state !== 4 || !panel.call.peerCamera
            }
            Rectangle {
                visible: panel.call.video && panel.call.state === 4
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                width: Math.min(110, parent.width * 0.3)
                height: Math.min(130, parent.height * 0.4)
                color: "#293a43"
                VideoOutput { id: localVideo; anchors.fill: parent; mirrored: panel.call.front; fillMode: VideoOutput.PreserveAspectFit; visible: panel.call.camera }
                Label { anchors.centerIn: parent; visible: !panel.call.camera; text: qsTr("摄像头已关闭"); color: "white"; font.pixelSize: 10 }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button { visible: panel.call.state === 2; text: qsTr("接听"); enabled: !panel.media.permissionPending; onClicked: panel.media.acceptCall(); Material.background: "#07a956"; Material.foreground: "white" }
            Button { visible: panel.call.state === 4; text: panel.call.muted ? qsTr("取消静音") : qsTr("静音"); onClicked: panel.media.setMuted(!panel.call.muted) }
            Button { visible: panel.call.state === 4 && panel.call.video; text: panel.call.camera ? qsTr("关闭摄像头") : qsTr("开启摄像头"); onClicked: panel.media.setCameraEnabled(!panel.call.camera) }
            Button { visible: panel.call.state === 4 && panel.call.video && panel.call.camera; text: qsTr("切换镜头"); onClicked: panel.media.switchCamera() }
            Button { text: panel.call.state === 2 ? qsTr("拒接") : panel.call.state === 1 ? qsTr("取消呼叫") : qsTr("挂断"); onClicked: panel.media.endCall(); Material.background: "#d74242"; Material.foreground: "white" }
        }
    }
    Component.onCompleted: media.setVideoSinks(localVideo.videoSink, remoteVideo.videoSink)
}
