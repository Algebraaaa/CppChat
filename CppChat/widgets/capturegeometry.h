#pragma once
#include <QRectF>
#include <QSize>
#include <QtMath>

namespace CaptureGeometry {
inline QRect pixelRect(const QRectF &selection, const QSize &logicalSize, const QSize &pixelSize)
{
  if (logicalSize.isEmpty() || pixelSize.isEmpty())
    return {};
  const QRectF clipped = selection.normalized().intersected(QRectF(QPointF(), logicalSize));
  if (clipped.isEmpty())
    return {};
  const qreal scaleX = qreal(pixelSize.width()) / logicalSize.width();
  const qreal scaleY = qreal(pixelSize.height()) / logicalSize.height();
  const int left = qFloor(clipped.left() * scaleX);
  const int top = qFloor(clipped.top() * scaleY);
  const int right = qCeil(clipped.right() * scaleX);
  const int bottom = qCeil(clipped.bottom() * scaleY);
  return QRect(left, top, right - left, bottom - top).intersected(QRect(QPoint(), pixelSize));
}

} // namespace CaptureGeometry
