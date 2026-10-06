#include "picturebubble.h"
#include "common/imageloader.h"
#include "network/resourcemgr.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

namespace {
const QSize kBubblePreviewSize(240, 220);

void loadOriginal(const QString &fileId, QObject *receiver, ImageLoader::Callback callback)
{
  auto *resources = ResourceMgr::instance();
  const auto bytes = resources->cachedImage(fileId);
  if (!bytes.isEmpty()) {
    ImageLoader::instance()->load(fileId, bytes, {}, 1, receiver, std::move(callback));
    return;
  }
  // 原始压缩字节可能已被有界缓存淘汰，按需从本机附件或服务端取回。
  const auto connection = std::make_shared<QMetaObject::Connection>();
  const QPointer<QObject> guard(receiver);
  *connection =
      QObject::connect(resources, &ResourceMgr::imageReady, receiver,
                       [fileId, guard, callback, connection](
                           const QString &id, const QByteArray &bytes, const QString &error) {
                         if (id != fileId)
                           return;
                         QObject::disconnect(*connection);
                         if (!guard)
                           return;
                         if (!error.isEmpty())
                           callback({{}, error});
                         else
                           ImageLoader::instance()->load(fileId, bytes, {}, 1, guard, callback);
                       });
  resources->fetchImage(fileId);
}

// 缩放查看时直接绘制原图，避免反复放大已经缩小的预览。
class ImageCanvas : public QWidget
{
public:
  explicit ImageCanvas(const QPixmap &picture) : _picture(picture)
  {
    setObjectName("image_preview_canvas");
    resize(picture.size());
  }
  void setPicture(const QPixmap &picture)
  {
    _picture = picture;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(rect(), _picture);
  }

private:
  QPixmap _picture;
};

class ImagePreviewDialog : public QDialog
{
public:
  ImagePreviewDialog(const QPixmap &picture, QWidget *parent)
      : QDialog(parent), _size(picture.size())
  {
    setObjectName("image_preview_dialog");
    setWindowTitle(tr("查看图片"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    auto *tools = new QHBoxLayout;
    _info = new QLabel(this);
    tools->addWidget(_info, 1);
    auto *smaller = new QPushButton(tr("缩小"), this);
    auto *larger = new QPushButton(tr("放大"), this);
    auto *original = new QPushButton(tr("原始大小"), this);
    original->setObjectName("image_original_size");
    auto *fit = new QPushButton(tr("适应窗口"), this);
    for (auto *button : {smaller, larger, original, fit}) {
      tools->addWidget(button);
      button->setEnabled(!picture.isNull());
      _buttons.append(button);
    }
    layout->addLayout(tools);
    _scroll = new QScrollArea(this);
    _scroll->setAlignment(Qt::AlignCenter);
    _canvas = new ImageCanvas(picture);
    _scroll->setWidget(_canvas);
    layout->addWidget(_scroll, 1);
    connect(smaller, &QPushButton::clicked, this, [this] { setZoom(_zoom / 1.25); });
    connect(larger, &QPushButton::clicked, this, [this] { setZoom(_zoom * 1.25); });
    connect(original, &QPushButton::clicked, this, [this] { setZoom(1.0); });
    connect(fit, &QPushButton::clicked, this, [this] { fitToWindow(); });
    const auto available = screen()->availableGeometry().size();
    resize(qMin(900, available.width() * 4 / 5), qMin(680, available.height() * 4 / 5));
    QTimer::singleShot(0, this, [this] { fitToWindow(); });
    if (picture.isNull())
      _info->setText(tr("正在加载原图…"));
  }
  void setImage(const LoadedImage &result)
  {
    if (result.image.isNull()) {
      _info->setText(result.error);
      return;
    }
    _size = result.image.size();
    _canvas->setPicture(QPixmap::fromImage(result.image));
    for (auto *button : _buttons)
      button->setEnabled(true);
    fitToWindow();
  }

private:
  void setZoom(qreal zoom)
  {
    _zoom = qBound(0.05, zoom, 4.0);
    _canvas->setFixedSize(qMax(1, qRound(_size.width() * _zoom)),
                          qMax(1, qRound(_size.height() * _zoom)));
    _info->setText(
        tr("%1 × %2 · %3%").arg(_size.width()).arg(_size.height()).arg(qRound(_zoom * 100)));
    _canvas->update();
  }
  void fitToWindow()
  {
    if (_size.isEmpty())
      return;
    const auto area = _scroll->viewport()->size() - QSize(16, 16);
    setZoom(qMin(1.0,
                 qMin(qreal(area.width()) / _size.width(), qreal(area.height()) / _size.height())));
  }
  QSize _size;
  qreal _zoom = 1.0;
  QLabel *_info = nullptr;
  QScrollArea *_scroll = nullptr;
  ImageCanvas *_canvas = nullptr;
  QList<QPushButton *> _buttons;
};
} // namespace

PictureBubble::PictureBubble(ChatRole role, QWidget *parent) : BubbleFrame(role, parent)
{
  // 图片直接占据消息区域，不使用文字气泡的边距和小三角。
  layout()->setContentsMargins(0, 0, 0, 0);
  _label = new QLabel;
  _label->setObjectName("chat_picture");
  _label->setScaledContents(false);
  _label->setAlignment(Qt::AlignCenter);
  _label->setWordWrap(true);
  _label->setAttribute(Qt::WA_TransparentForMouseEvents);
  setWidget(_label);
  setPreview({});
  auto *resources = ResourceMgr::instance();
  connect(resources, &ResourceMgr::imageReady, this,
          [this](const QString &id, const QByteArray &bytes, const QString &error) {
            if (id != _fileId)
              return;
            if (error.isEmpty()) {
              if (!hasImage())
                loadPreview(bytes);
            } else if (!hasImage())
              setError(error);
          });
  connect(resources, &ResourceMgr::downloadProgress, this,
          [this](const QString &id, qint64 done, qint64 total) {
            if (id == _fileId)
              setProgress(done, total);
          });
  connect(this, &PictureBubble::retryRequested, this, [this] { loadImage(_fileId); });
}

void PictureBubble::loadImage(const QString &fileId)
{
  _fileId = fileId;
  ++_revision;
  const auto image =
      ImageLoader::instance()->cachedPreview(fileId, kBubblePreviewSize, devicePixelRatioF());
  if (!image.isNull()) {
    setPreview(image);
    return;
  }
  setPreview({});
  ResourceMgr::instance()->fetchImage(fileId);
}

void PictureBubble::loadPreview(const QByteArray &bytes)
{
  const auto revision = ++_revision;
  ImageLoader::instance()->load(_fileId, bytes, kBubblePreviewSize, devicePixelRatioF(), this,
                                [this, revision](const LoadedImage &result) {
                                  if (_revision != revision)
                                    return;
                                  if (result.image.isNull())
                                    setError(result.error);
                                  else
                                    setPreview(result.image);
                                });
}

void PictureBubble::setPreview(const QImage &image)
{
  _preview = QPixmap::fromImage(image);
  QSize displaySize(200, 52);
  _label->clear();
  if (_preview.isNull()) {
    _label->setText(tr("图片加载中…"));
    _label->setToolTip({});
    unsetCursor();
  } else {
    displaySize = _preview.deviceIndependentSize().toSize().expandedTo(QSize(1, 1));
    _label->setPixmap(_preview);
    _label->setToolTip(tr("点击查看原图"));
    setCursor(Qt::PointingHandCursor);
  }
  _label->setFixedSize(displaySize);
  setFixedSize(displaySize);
}

void PictureBubble::paintEvent(QPaintEvent *event)
{
  // 跳过 BubbleFrame 的绿色/白色圆角背景和小三角绘制。
  QFrame::paintEvent(event);
}

void PictureBubble::setError(const QString &message)
{
  setPreview({});
  _label->setText(message);
  _label->setToolTip(tr("%1\n点击图片重试").arg(message));
  setCursor(Qt::PointingHandCursor);
}

void PictureBubble::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton) {
    if (_preview.isNull())
      emit retryRequested();
    else if (_previewDialog) {
      _previewDialog->show();
      _previewDialog->raise();
      _previewDialog->activateWindow();
    } else {
      auto *dialog = new ImagePreviewDialog({}, window());
      _previewDialog = dialog;
      connect(this, &QObject::destroyed, dialog, &QWidget::close);
      dialog->show();
      loadOriginal(_fileId, dialog,
                   [dialog](const LoadedImage &result) { dialog->setImage(result); });
    }
    event->accept();
    return;
  }
  BubbleFrame::mousePressEvent(event);
}

void PictureBubble::setProgress(qint64 done, qint64 total)
{
  if (_preview.isNull())
    _label->setText(tr("图片下载中 %1/%2 KB").arg(done / 1024).arg(total / 1024));
}

void PictureBubble::copyOriginal()
{
  if (_copying || !hasImage())
    return;
  _copying = true;
  loadOriginal(_fileId, this, [this](const LoadedImage &result) {
    _copying = false;
    if (result.image.isNull())
      _label->setToolTip(tr("复制原图失败：%1").arg(result.error));
    else
      QGuiApplication::clipboard()->setImage(result.image);
  });
}
