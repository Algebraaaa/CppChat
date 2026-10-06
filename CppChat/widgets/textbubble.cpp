#include "textbubble.h"
#include "common/global.h"
#include "common/emojicatalog.h"
#include <QAbstractTextDocumentLayout>
#include <QDebug>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTimer>
#include <QtMath>

namespace {
// 字体宽度可能包含小数，并且 QTextDocument 布局时还会产生像素取整误差。
// 多预留几个像素，避免恰好把最后一个字符挤到下一行。
constexpr int kTextWidthSafetyPadding = 4;
} // namespace

TextBubble::TextBubble(ChatRole role, const QString &text, QWidget *parent)
    : BubbleFrame(role, parent)
{
  m_pTextEdit = new QTextEdit();
  m_pTextEdit->setObjectName("chat_message_text");
  m_pTextEdit->setReadOnly(true);
  m_pTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_pTextEdit, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
    emit messageContextMenuRequested(m_pTextEdit->viewport()->mapToGlobal(position));
  });
  m_pTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_pTextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_pTextEdit->installEventFilter(this);
  m_pTextEdit->viewport()->installEventFilter(this);
  setWidget(m_pTextEdit);
  connect(m_pTextEdit->document()->documentLayout(),
          &QAbstractTextDocumentLayout::documentSizeChanged, this,
          [this](const QSizeF &) { scheduleHeightUpdate(); });
  // 先应用 QSS 的实际字体，再计算气泡宽度，避免短消息换行或被截断。
  m_pTextEdit->ensurePolished();
  setPlainText(text);
  adjustTextHeight();
}

bool TextBubble::eventFilter(QObject *o, QEvent *e)
{
  // 绘制只负责绘制。滚动重绘时改变 fixedHeight 会反过来触发布局和 rangeChanged。
  if ((o == m_pTextEdit->viewport() && e->type() == QEvent::Resize) ||
      (o == m_pTextEdit && e->type() == QEvent::FontChange))
    scheduleHeightUpdate();
  return BubbleFrame::eventFilter(o, e);
}

void TextBubble::setPlainText(const QString &text)
{
  m_pTextEdit->clear();
  auto cursor = m_pTextEdit->textCursor();
  EmojiCatalog::InsertText(cursor, text);
  m_pTextEdit->setTextCursor(cursor);
  //找到段落中最大宽度
  qreal doc_margin = m_pTextEdit->document()->documentMargin();
  int margin_left = this->layout()->contentsMargins().left();
  int margin_right = this->layout()->contentsMargins().right();
  QFontMetricsF fm(m_pTextEdit->font());
  QTextDocument *doc = m_pTextEdit->document();
  qreal max_width = 0.0;
  //遍历每一段找到 最宽的那一段
  for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next()) //字体总长
  {
    qreal txtW = 0;
    for (auto fragment = it.begin(); !fragment.atEnd(); ++fragment) {
      const auto part = fragment.fragment();
      if (!part.isValid())
        continue;
      txtW += part.charFormat().isImageFormat()
                  ? part.charFormat().toImageFormat().width() * part.length()
                  : fm.horizontalAdvance(part.text());
    }
    max_width = max_width < txtW ? txtW : max_width; // 找到最长的那段
  }
  //设置这个气泡的最大宽度 只需要设置一次
  const int bubbleWidth =
      qCeil(max_width + doc_margin * 2) + margin_left + margin_right + kTextWidthSafetyPadding;
  setMaximumWidth(bubbleWidth);
}

void TextBubble::scheduleHeightUpdate()
{
  if (_heightUpdatePending)
    return;
  _heightUpdatePending = true;
  QTimer::singleShot(0, this, [this] {
    _heightUpdatePending = false;
    adjustTextHeight();
  });
}

void TextBubble::adjustTextHeight()
{
  // documentSize 已包含段落与 documentMargin，避免每次绘制遍历所有文本段落。
  const qreal documentHeight = m_pTextEdit->document()->documentLayout()->documentSize().height();
  const auto margins = layout()->contentsMargins();
  const int desiredHeight = qMax(1, qCeil(documentHeight) + margins.top() + margins.bottom() +
                                      m_pTextEdit->frameWidth() * 2);
  if (minimumHeight() != desiredHeight || maximumHeight() != desiredHeight)
    setFixedHeight(desiredHeight);
}
