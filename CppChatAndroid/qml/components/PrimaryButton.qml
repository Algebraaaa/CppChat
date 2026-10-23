import QtQuick
import QtQuick.Controls.Material

Button {
    id: root
    implicitWidth: Math.max(72, contentItem.implicitWidth + 32)
    implicitHeight: 56
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    background: Rectangle { radius: 8; color: root.enabled ? (root.down ? "#078844" : "#07a956") : "#98a2b3" }
    contentItem: Label { text: root.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font: root.font }
}
