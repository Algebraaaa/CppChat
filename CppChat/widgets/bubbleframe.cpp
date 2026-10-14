#include "bubbleframe.h"
#include <QDebug>
#include <QPainter>
#include <QVariant>
const int WIDTH_SANJIAO = 8; // 三角宽
BubbleFrame::BubbleFrame(ChatRole role, QWidget *parent)
    : QFrame(parent), m_role(role), m_margin(4),
      m_backgroundColor(role == ChatRole::Self ? QColor(149, 236, 105) : QColor(Qt::white))
{
  // 用途在首次 polish 前确定；QSS 通过 backgroundColor 同时设置圆角背景和箭头。
  setProperty("chatRole", role == ChatRole::Self ? QStringLiteral("self") : QStringLiteral("other"));
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
    emit messageContextMenuRequested(mapToGlobal(position));
  });
  m_pHLayout = new QHBoxLayout();
  if (m_role == ChatRole::Self)
    m_pHLayout->setContentsMargins(m_margin, m_margin, WIDTH_SANJIAO + m_margin, m_margin);
  else
    m_pHLayout->setContentsMargins(WIDTH_SANJIAO + m_margin, m_margin, m_margin, m_margin);

  this->setLayout(m_pHLayout);
}

QColor BubbleFrame::backgroundColor() const
{
  return m_backgroundColor;
}

void BubbleFrame::setBackgroundColor(const QColor &color)
{
  if (!color.isValid() || color == m_backgroundColor)
    return;
  m_backgroundColor = color;
  update();
}

void BubbleFrame::setMargin(int margin)
{
  Q_UNUSED(margin);
  // m_margin = margin;
}

void BubbleFrame::setWidget(QWidget *w)
{
  if (m_pHLayout->count() > 0)
    return;
  else {
    m_pHLayout->addWidget(w);
  }
}

void BubbleFrame::paintEvent(QPaintEvent *e)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QBrush(m_backgroundColor));

  if (m_role == ChatRole::Other) {
    // 画气泡
    QRect bk_rect = QRect(WIDTH_SANJIAO, 0, this->width() - WIDTH_SANJIAO, this->height());
    painter.drawRoundedRect(bk_rect, 9, 9);
    // 画小三角
    QPointF points[3] = {
        QPointF(bk_rect.x(), 12),
        QPointF(bk_rect.x(), 10 + WIDTH_SANJIAO + 2),
        QPointF(bk_rect.x() - WIDTH_SANJIAO, 10 + WIDTH_SANJIAO - WIDTH_SANJIAO / 2),
    };
    painter.drawPolygon(points, 3);
  } else {
    // 画气泡
    QRect bk_rect = QRect(0, 0, this->width() - WIDTH_SANJIAO, this->height());
    painter.drawRoundedRect(bk_rect, 9, 9);
    // 画三角
    QPointF points[3] = {
        QPointF(bk_rect.x() + bk_rect.width(), 12),
        QPointF(bk_rect.x() + bk_rect.width(), 12 + WIDTH_SANJIAO + 2),
        QPointF(bk_rect.x() + bk_rect.width() + WIDTH_SANJIAO,
                10 + WIDTH_SANJIAO - WIDTH_SANJIAO / 2),
    };
    painter.drawPolygon(points, 3);
  }

  return QFrame::paintEvent(e);
}
