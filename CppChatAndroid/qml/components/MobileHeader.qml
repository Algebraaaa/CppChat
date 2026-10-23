import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

ToolBar {
    id: root
    property string title: ""
    signal back()
    property string actionText: ""
    signal action()
    Material.background: "#07a956"
    Material.foreground: "white"
    background: Rectangle { color: "#07a956" }
    RowLayout {
        anchors.fill: parent
        ToolButton { text: "‹"; font.pixelSize: 28; implicitWidth: 56; implicitHeight: 56; onClicked: root.back(); Accessible.name: qsTr("返回") }
        Label { text: root.title; Layout.fillWidth: true; elide: Text.ElideRight; font.bold: true; font.pixelSize: 18 }
        ToolButton { visible: root.actionText.length > 0; text: root.actionText; implicitHeight: 56; onClicked: root.action() }
    }
}
