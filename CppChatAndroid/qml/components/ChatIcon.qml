import QtQuick

Canvas {
    id: icon
    property string name: "chat"
    property color color: "#191919"
    implicitWidth: 24
    implicitHeight: 24
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        ctx.scale(width / 24, height / 24);
        ctx.strokeStyle = color;
        ctx.fillStyle = color;
        ctx.lineWidth = 1.7;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        function line(points) {
            ctx.beginPath();
            ctx.moveTo(points[0], points[1]);
            for (let i = 2; i < points.length; i += 2)
                ctx.lineTo(points[i], points[i + 1]);
            ctx.stroke();
        }
        function circle(x, y, radius) {
            ctx.beginPath();
            ctx.arc(x, y, radius, 0, Math.PI * 2);
            ctx.stroke();
        }
        switch (name) {
        case "back":
            line([15, 4, 7, 12, 15, 20]);
            break;
        case "plus":
            circle(12, 12, 9);
            line([12, 7, 12, 17]);
            line([7, 12, 17, 12]);
            break;
        case "search":
            circle(10, 10, 6.5);
            line([15, 15, 21, 21]);
            break;
        case "more":
            for (let x of [5, 12, 19]) {
                ctx.beginPath();
                ctx.arc(x, 12, 1.4, 0, Math.PI * 2);
                ctx.fill();
            }
            break;
        case "contacts":
            circle(10, 8, 3.2);
            line([3, 20, 3, 17, 6, 13, 14, 13, 17, 17, 17, 20]);
            line([18, 5, 21, 5, 21, 19]);
            break;
        case "user":
            circle(12, 7, 4);
            line([4, 21, 4, 18, 7, 14, 17, 14, 20, 18, 20, 21]);
            break;
        case "voice":
            line([5, 9, 5, 15]);
            line([9, 6, 9, 18]);
            line([13, 3, 13, 21]);
            line([17, 6, 17, 18]);
            line([21, 9, 21, 15]);
            break;
        case "image":
            ctx.strokeRect(3, 4, 18, 16);
            circle(8, 9, 1.5);
            line([4, 18, 11, 12, 15, 15, 18, 12, 21, 15]);
            break;
        case "file":
            line([6, 2, 15, 2, 20, 7, 20, 22, 6, 22, 6, 2]);
            line([14, 2, 14, 8, 20, 8]);
            break;
        case "video":
            ctx.strokeRect(3, 6, 12, 12);
            line([15, 10, 21, 7, 21, 17, 15, 14]);
            break;
        case "settings":
            circle(12, 12, 8);
            circle(12, 12, 3);
            break;
        case "chevron":
            line([9, 5, 16, 12, 9, 19]);
            break;
        case "chat":
        default:
            line([4, 4, 20, 4, 22, 6, 22, 16, 20, 18, 10, 18, 5, 22, 5, 18, 2, 16, 2, 6, 4, 4]);
            break;
        }
    }
}
