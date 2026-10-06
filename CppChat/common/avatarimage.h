#ifndef AVATARIMAGE_H
#define AVATARIMAGE_H

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QtMath>

// QLabel 的 border-radius 不会裁剪 pixmap，头像需要在绘制时裁掉四角。
inline QPixmap roundedAvatarPixmap(const QPixmap &source, const QSize &size,
                                   qreal devicePixelRatio = 1.0)
{
  if (source.isNull() || size.isEmpty())
    return {};

  const qreal ratio = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
  const QString key = QString("cppchat/rounded-avatar/%1/%2x%3@%4")
                          .arg(source.cacheKey())
                          .arg(size.width())
                          .arg(size.height())
                          .arg(ratio, 0, 'g', 12);
  QPixmap rounded;
  if (QPixmapCache::find(key, &rounded))
    return rounded;

  const QSize pixels(qCeil(size.width() * ratio), qCeil(size.height() * ratio));
  QPixmap scaled = source.scaled(pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  scaled.setDevicePixelRatio(ratio);
  rounded = QPixmap(pixels);
  rounded.setDevicePixelRatio(ratio);
  rounded.fill(Qt::transparent);

  QPainter painter(&rounded);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  const int radius = qMin(16, qMin(size.width(), size.height()) / 6);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);
  painter.setClipPath(clip);
  painter.drawPixmap(QPointF((size.width() - scaled.width() / ratio) / 2,
                             (size.height() - scaled.height() / ratio) / 2),
                     scaled);
  painter.end();
  // Qt 的有界缓存会淘汰旧项；头像内容和屏幕缩放比例变化时自动使用新键。
  QPixmapCache::insert(key, rounded);
  return rounded;
}

#endif // AVATARIMAGE_H
