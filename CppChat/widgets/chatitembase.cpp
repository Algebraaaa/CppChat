#include "chatitembase.h"
#include "bubbleframe.h"
#include "common/avatarimage.h"
#include <QVBoxLayout>
ChatItemBase::ChatItemBase(ChatRole role, QWidget *parent) : QWidget(parent), m_role(role)
{
  m_pNameLabel = new QLabel();
  m_pNameLabel->setObjectName("chat_user_name");
  m_pNameLabel->setFixedHeight(20);
  m_pIconLabel = new QLabel();
  m_pIconLabel->setScaledContents(true);
  m_pIconLabel->setFixedSize(42, 42);
  m_pBubble = new QWidget();
  QGridLayout *pGLayout = new QGridLayout();
  pGLayout->setVerticalSpacing(3);
  pGLayout->setHorizontalSpacing(3);
  pGLayout->setContentsMargins(3, 3, 3, 3);
  QSpacerItem *pSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
  if (m_role == ChatRole::Self) {
    m_pNameLabel->setContentsMargins(0, 0, 8, 0);
    m_pNameLabel->setAlignment(Qt::AlignRight);
    pGLayout->addWidget(m_pNameLabel, 0, 1, 1, 1);
    pGLayout->addWidget(m_pIconLabel, 0, 2, 2, 1, Qt::AlignTop);
    pGLayout->addItem(pSpacer, 1, 0, 1, 1);
    pGLayout->addWidget(m_pBubble, 1, 1, 1, 1, Qt::AlignRight | Qt::AlignTop);
    pGLayout->setColumnStretch(0, 2);
    pGLayout->setColumnStretch(1, 3);
  } else {
    m_pNameLabel->setContentsMargins(8, 0, 0, 0);
    m_pNameLabel->setAlignment(Qt::AlignLeft);
    pGLayout->addWidget(m_pIconLabel, 0, 0, 2, 1, Qt::AlignTop);
    pGLayout->addWidget(m_pNameLabel, 0, 1, 1, 1);
    pGLayout->addWidget(m_pBubble, 1, 1, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    pGLayout->addItem(pSpacer, 2, 2, 1, 1);
    pGLayout->setColumnStretch(1, 3);
    pGLayout->setColumnStretch(2, 2);
  }
  m_pStatusLabel = new QLabel(this);
  m_pStatusLabel->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_pStatusLabel, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
    emit messageContextMenuRequested(m_pStatusLabel->mapToGlobal(position));
  });
  m_pStatusLabel->setObjectName("message_status");
  m_pStatusLabel->setAlignment(m_role == ChatRole::Self ? Qt::AlignRight : Qt::AlignLeft);
  pGLayout->addWidget(m_pStatusLabel, 2, 1);
  m_pStatusLabel->hide();
  this->setLayout(pGLayout);
}

void ChatItemBase::setUserName(const QString &name)
{
  m_pNameLabel->setText(name);
  m_pNameLabel->setVisible(!name.trimmed().isEmpty());
}

void ChatItemBase::setUserIcon(const QPixmap &icon)
{
  m_pIconLabel->setPixmap(roundedAvatarPixmap(icon, m_pIconLabel->size()));
}

void ChatItemBase::setWidget(QWidget *w)
{
  QGridLayout *pGLayout = (qobject_cast<QGridLayout *>)(this->layout());
  if (!w)
    return;
  delete pGLayout->replaceWidget(m_pBubble, w);
  delete m_pBubble;
  m_pBubble = w;
  if (auto *bubble = qobject_cast<BubbleFrame *>(w))
    connect(bubble, &BubbleFrame::messageContextMenuRequested, this,
            &ChatItemBase::messageContextMenuRequested);
  pGLayout->setAlignment(m_pBubble, m_role == ChatRole::Self ? Qt::AlignRight | Qt::AlignTop
                                                             : Qt::AlignLeft | Qt::AlignTop);
}

void ChatItemBase::setStatus(int status)
{
  if (m_role != ChatRole::Self)
    return;
  const QString text = status < 0 ? tr("发送中")
      : (status == SEND_FAILED ? tr("未确认 · 右键重试") : tr("已发送"));
  if (!m_pStatusLabel->isHidden() && m_pStatusLabel->text() == text &&
      m_pStatusLabel->property("failed").toBool() == (status == SEND_FAILED))
    return;
  m_pStatusLabel->setText(text);
  m_pStatusLabel->setProperty("failed", status == SEND_FAILED);
  repolish(m_pStatusLabel);
  m_pStatusLabel->show();
}
