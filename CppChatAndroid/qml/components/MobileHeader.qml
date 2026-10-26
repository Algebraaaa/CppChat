import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "ChatStyle.js" as Style

ToolBar {
    id: root
    property string title: ""
    signal back()
    property string actionText: ""
    signal action()
    implicitHeight: 56
    Material.background: Style.toolbar
    Material.foreground: Style.text
    background: Rectangle {
        color: Style.toolbar
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Style.separator }
    }
    RowLayout {
        anchors.fill: parent
        ToolButton {
            implicitWidth: 52; implicitHeight: 56
            contentItem: Item { ChatIcon { anchors.centerIn: parent; name: "back"; width: 22; height: 22 } }
            onClicked: root.back()
            Accessible.name: qsTr("返回")
        }
        Label { text: root.title; color: Style.text; Layout.fillWidth: true; elide: Text.ElideRight; font.weight: Font.DemiBold; font.pixelSize: 18; textFormat: Text.PlainText }
        ToolButton { visible: root.actionText.length > 0; text: root.actionText; implicitWidth: 52; implicitHeight: 56; onClicked: root.action(); Accessible.name: root.actionText === "⋯" ? qsTr("更多操作") : root.actionText }
    }
}
