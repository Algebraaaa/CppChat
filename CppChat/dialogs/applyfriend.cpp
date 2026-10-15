#include "applyfriend.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "ui_applyfriend.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <algorithm>

namespace {
constexpr int labelMargin = 4;
constexpr int labelSpacing = 8;
} // namespace

ApplyFriend::ApplyFriend(QWidget *parent) : QDialog(parent), ui(new Ui::ApplyFriend)
{
  ui->setupUi(this);
  // 隐藏对话框标题栏
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  ui->apply_lb->setAttribute(Qt::WA_TransparentForMouseEvents);
  connect(ui->close_btn, &QPushButton::clicked, this, &ApplyFriend::reject);
  this->setObjectName("ApplyFriend");
  this->setModal(true);
  ui->name_ed->setPlaceholderText(tr("我是 %1").arg(UserMgr::GetInstance()->GetName()));
  ui->lb_ed->setPlaceholderText("搜索、添加标签");
  ui->back_ed->setPlaceholderText(tr("好友备注"));
  ui->back_ed->setMaxLength(64);

  ui->lb_ed->SetMaxLength(21);
  ui->lb_ed->setFixedHeight(28);
  ui->input_tip_wid->hide();

  ui->lb_list->installEventFilter(this);
  ui->gridWidget->installEventFilter(this);

  _tip_data = {"同学",          "家人",           "菜鸟教程",       "C++ Primer",
               "Rust 程序设计", "父与子学Python", "nodejs开发指南", "go 语言开发指南",
               "游戏伙伴",      "金融投资",       "微信读书",       "拼多多拼友"};

  connect(ui->more_lb, &ClickedOnceLabel::clicked, this, &ApplyFriend::ShowMoreLabel);
  InitTipLbs();
  // 链接输入标签回车事件
  connect(ui->lb_ed, &CustomizeEdit::returnPressed, this, &ApplyFriend::SlotLabelEnter);
  connect(ui->lb_ed, &CustomizeEdit::textChanged, this, &ApplyFriend::SlotLabelTextChange);
  connect(ui->lb_ed, &CustomizeEdit::editingFinished, this, &ApplyFriend::SlotLabelEditFinished);
  connect(ui->tip_lb, &ClickedOnceLabel::clicked, this, &ApplyFriend::SlotAddFirendLabelByClickTip);

  ui->scrollArea->horizontalScrollBar()->setHidden(true);
  ui->scrollArea->verticalScrollBar()->setHidden(true);
  ui->scrollArea->installEventFilter(this);
  ui->sure_btn->SetState("normal", "hover", "press");
  auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_contact_tags_updated, this, [this](int uid) {
    if (_pending && _contactInfo && uid == _contactInfo->_uid) {
      _pending = false;
      if (_contactInfo->_back != ui->back_ed->text().trimmed()) {
        ui->sure_btn->setEnabled(true);
        ui->close_btn->setEnabled(true);
        ui->apply_wid->setEnabled(true);
        QMessageBox::warning(this, tr("备注未保存"),
                             tr("服务器未确认备注修改，请更新服务器后重试"));
        return;
      }
      accept();
    }
  });
  connect(tcp.get(), &TcpMgr::sig_friend_deleted, this, [this](int uid) {
    if (_contactInfo && uid == _contactInfo->_uid) {
      _pending = false;
      QDialog::reject();
    }
  });
  connect(tcp.get(), &TcpMgr::sig_add_friend_rsp, this, [this](int uid) {
    if (_pending && _si && uid == _si->_uid) {
      _pending = false;
      accept();
    }
  });
  connect(tcp.get(), &TcpMgr::sig_auth_rsp, this, [this](std::shared_ptr<AuthRsp> auth) {
    if (!_pending || !_applyInfo || auth->_uid != _applyInfo->_uid)
      return;
    const auto friendInfo = UserMgr::GetInstance()->GetFriendById(auth->_uid);
    if (friendInfo)
      friendInfo->_back = ui->back_ed->text().trimmed();
    _pending = false;
    accept();
  });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId request, const QString &message) {
            const auto expected = requestId();
            if (!_pending || request != expected)
              return;
            _pending = false;
            ui->sure_btn->setEnabled(true);
            ui->close_btn->setEnabled(true);
            ui->apply_wid->setEnabled(true);
            QMessageBox::warning(this, tr("操作失败"), message);
          });
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, &QDialog::reject);
  connect(tcp.get(), &TcpMgr::sig_notify_offline, this, &QDialog::reject);
}

ApplyFriend::~ApplyFriend()
{
  qDebug() << "ApplyFriend destruct";
  delete ui;
}
void ApplyFriend::InitTipLbs()
{
  for (const auto &text : _tip_data)
    createTipLabel(text);
  layoutTipLabels();
  resetLabels();
}

ClickedLabel *ApplyFriend::createTipLabel(const QString &text)
{
  auto *label = new ClickedLabel(ui->lb_list);
  label->setObjectName("tipslb");
  label->SetState("normal", "hover", "pressed", "selected_normal", "selected_hover",
                  "selected_pressed");
  label->setText(text);
  label->setTextFormat(Qt::PlainText);
  label->setProperty("selected", false);
  connect(label, &ClickedLabel::clicked, this, &ApplyFriend::SlotChangeFriendLabelByTip);
  _add_labels.insert(text, label);
  _add_label_keys.push_back(text);
  return label;
}

void ApplyFriend::layoutTipLabels()
{
  int x = labelMargin;
  int y = labelMargin;
  int rowHeight = 0;
  int row = 1;
  int contentHeight = 0;
  bool hasHiddenLabels = false;
  const int availableWidth = ui->lb_list->width() - 2 * labelMargin;
  if (availableWidth <= 0)
    return;

  for (const auto &text : _add_label_keys) {
    auto *label = _add_labels.value(text);
    // sizeHint 包含 QSS 的字体、padding 和边框，不能只用文字宽度排位置。
    label->ensurePolished();
    const QSize size = label->sizeHint();
    label->setFixedSize(qMin(size.width(), availableWidth), size.height());
    if (x > labelMargin && x + label->width() > ui->lb_list->width() - labelMargin) {
      x = labelMargin;
      y += rowHeight + labelSpacing;
      rowHeight = 0;
      ++row;
    }
    const bool visible = _tipsExpanded || row <= 2;
    label->setVisible(visible);
    if (!visible) {
      hasHiddenLabels = true;
      continue;
    }
    label->move(x, y);
    rowHeight = qMax(rowHeight, label->height());
    contentHeight = y + rowHeight + labelMargin;
    x += label->width() + labelSpacing;
  }
  ui->lb_list->setFixedHeight(contentHeight);
  ui->more_lb_wid->setVisible(!_tipsExpanded && hasHiddenLabels);
}

bool ApplyFriend::eventFilter(QObject *obj, QEvent *event)
{
  if (event->type() == QEvent::Resize) {
    if (obj == ui->lb_list)
      layoutTipLabels();
    else if (obj == ui->gridWidget)
      resetLabels();
  }
  if (obj == ui->scrollArea && event->type() == QEvent::Enter) {
    ui->scrollArea->verticalScrollBar()->setHidden(false);
  } else if (obj == ui->scrollArea && event->type() == QEvent::Leave) {
    ui->scrollArea->verticalScrollBar()->setHidden(true);
  }
  return QObject::eventFilter(obj, event);
}
void ApplyFriend::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
  _si = si;
  auto applyname = UserMgr::GetInstance()->GetName();
  auto bakname = si->_name;
  ui->name_ed->setText(applyname);
  ui->back_ed->setText(bakname);
}
void ApplyFriend::ShowMoreLabel()
{
  _tipsExpanded = true;
  layoutTipLabels();
}

void ApplyFriend::resetLabels()
{
  int x = labelMargin;
  int y = labelMargin;
  int rowHeight = 0;
  const int right = ui->gridWidget->width() - labelMargin;
  const int availableWidth = right - labelMargin;
  if (availableWidth <= 0)
    return;

  // 保持选择顺序；新增、删除和容器尺寸变化都按完整控件尺寸重排。
  for (const auto &text : _friend_label_keys) {
    auto *label = _friend_labels.value(text);
    label->setFixedWidth(qMin(label->Width(), availableWidth));
    if (x > labelMargin && x + label->width() > right) {
      x = labelMargin;
      y += rowHeight + labelSpacing;
      rowHeight = 0;
    }
    label->move(x, y);
    label->show();
    rowHeight = qMax(rowHeight, label->height());
    x += label->width() + labelSpacing;
  }

  const int inputWidth = qMin(availableWidth, 150);
  if (x > labelMargin && x + inputWidth > right) {
    x = labelMargin;
    y += rowHeight + labelSpacing;
    rowHeight = 0;
  }
  ui->lb_ed->setFixedWidth(inputWidth);
  ui->lb_ed->move(x, y);
  rowHeight = qMax(rowHeight, ui->lb_ed->height());
  ui->gridWidget->setFixedHeight(y + rowHeight + labelMargin);
}

void ApplyFriend::addLabel(QString name)
{
  if (_friend_labels.contains(name))
    return;
  if (_friend_labels.size() >= 12 || name.toUtf8().size() > 128) {
    QMessageBox::information(this, tr("标签限制"), tr("最多设置 12 个标签，请缩短过长的标签名称"));
    return;
  }
  auto *label = new FriendLabel(ui->gridWidget);
  label->setObjectName("FriendLabel");
  label->SetText(name);
  _friend_labels.insert(name, label);
  _friend_label_keys.push_back(name);
  connect(label, &FriendLabel::sig_close, this, &ApplyFriend::SlotRemoveFriendLabel);
  resetLabels();
  ui->lb_ed->clear();
}

void ApplyFriend::selectLabel(const QString &text)
{
  const QString name = text.trimmed();
  if (name.isEmpty())
    return;
  addLabel(name);
  if (!_friend_labels.contains(name))
    return;
  auto *label = _add_labels.value(name, nullptr);
  if (!label) {
    _tip_data.push_back(name);
    label = createTipLabel(name);
  }
  // 新输入的标签或折叠区中的标签需要展开后才能看见。
  if (label->isHidden())
    _tipsExpanded = true;
  label->SetCurState(ClickLbState::Selected);
  label->setProperty("selected", true);
  repolish(label);
  layoutTipLabels();
  ui->input_tip_wid->hide();
}

void ApplyFriend::SlotLabelEnter() { selectLabel(ui->lb_ed->text()); }

void ApplyFriend::SlotRemoveFriendLabel(QString name)
{
  qDebug() << "receive close signal";

  auto find_iter = _friend_labels.find(name);

  if (find_iter == _friend_labels.end()) {
    return;
  }

  auto find_key = _friend_label_keys.end();
  for (auto iter = _friend_label_keys.begin(); iter != _friend_label_keys.end(); iter++) {
    if (*iter == name) {
      find_key = iter;
      break;
    }
  }

  if (find_key != _friend_label_keys.end()) {
    _friend_label_keys.erase(find_key);
  }

  // 关闭图标的点击事件还在执行，等事件处理结束后再销毁标签。
  find_iter.value()->hide();
  find_iter.value()->deleteLater();
  _friend_labels.erase(find_iter);

  resetLabels();

  auto find_add = _add_labels.find(name);
  if (find_add == _add_labels.end()) {
    return;
  }

  find_add.value()->setProperty("selected", false);
  find_add.value()->ResetNormalState();
}

// 点击已有标签，添加或删除当前弹窗中选中的标签。
void ApplyFriend::SlotChangeFriendLabelByTip(QString lbtext, ClickLbState state)
{
  auto find_iter = _add_labels.find(lbtext);
  if (find_iter == _add_labels.end()) {
    return;
  }

  // clicked 带的是点击前状态，由此处决定选中或取消。
  if (state == ClickLbState::Normal)
    selectLabel(lbtext);
  else if (state == ClickLbState::Selected)
    SlotRemoveFriendLabel(lbtext);
}
void ApplyFriend::SlotLabelTextChange(const QString &text)
{
  if (text.isEmpty()) {
    ui->tip_lb->setText("");
    ui->input_tip_wid->hide();
    return;
  }

  auto iter = std::find(_tip_data.begin(), _tip_data.end(), text);
  if (iter == _tip_data.end()) {
    auto new_text = add_prefix + text;
    ui->tip_lb->setText(new_text);
    ui->input_tip_wid->show();
    return;
  }
  ui->tip_lb->setText(text);
  ui->input_tip_wid->show();
}
void ApplyFriend::SlotLabelEditFinished() { ui->input_tip_wid->hide(); }
void ApplyFriend::SlotAddFirendLabelByClickTip(QString text)
{
  int index = text.indexOf(add_prefix);
  if (index != -1) {
    text = text.mid(index + add_prefix.length());
  }
  selectLabel(text);
}

void ApplyFriend::SetApplyInfo(std::shared_ptr<ApplyInfo> info)
{
  _applyInfo = info;
  ui->apply_lb->setText(tr("通过好友申请"));
  ui->label->hide();
  ui->name_ed->hide();
  ui->back_ed->setText(info->_name);
  ui->sure_btn->setText(tr("接受"));
}

ReqId ApplyFriend::requestId() const
{
  if (_contactInfo)
    return ID_SET_CONTACT_TAGS_REQ;
  return _applyInfo ? ID_AUTH_FRIEND_REQ : ID_ADD_FRIEND_REQ;
}

void ApplyFriend::SetContactInfo(std::shared_ptr<UserInfo> info)
{
  _contactInfo = info;
  ui->apply_lb->setText(tr("设置备注和标签"));
  ui->name_ed->hide();
  ui->back_ed->setText(info ? info->_back : QString());
  ui->label->hide();
  ui->sure_btn->setText(tr("保存"));
  if (info) {
    for (const auto &tag : info->_tags)
      selectLabel(tag);
  }
}

void ApplyFriend::reject()
{
  // 此协议的认证回包不带请求流水号，一次只允许一个申请/认证操作等待回包。
  if (_pending && TcpMgr::GetInstance()->IsConnected())
    return;
  QDialog::reject();
}

void ApplyFriend::on_sure_btn_clicked()
{
  if (_pending || (!_si && !_applyInfo && !_contactInfo))
    return;
  QJsonObject request;
  const int uid = UserMgr::GetInstance()->GetUid();
  const int target =
      _contactInfo ? _contactInfo->_uid : (_applyInfo ? _applyInfo->_uid : _si->_uid);
  if (uid <= 0 || target <= 0 || target == uid)
    return;
  const QString back = ui->back_ed->text().trimmed();
  const ReqId id = requestId();
  if (_contactInfo) {
    request = {{"friend_uid", target}, {"back", back}};
  } else if (_applyInfo) {
    request = {{"fromuid", uid}, {"touid", target}, {"back", back}};
  } else {
    const auto name = ui->name_ed->text().trimmed();
    request = {{"uid", uid},
               {"touid", target},
               {"bakname", back},
               {"applyname", name.isEmpty() ? ui->name_ed->placeholderText() : name}};
    // 对方通过申请时，通知包在同服转发场景下可能误带当前用户资料；
    // 暂存搜索到的真实资料，收到通知后按对方 uid 取回。
    UserMgr::GetInstance()->RememberOutgoingApply(_si);
  }
  QJsonArray tags;
  for (const auto &text : _friend_label_keys)
    tags.append(text);
  request["tags"] = tags;
  _pending = true;
  ui->apply_wid->setEnabled(false);
  ui->sure_btn->setEnabled(false);
  ui->close_btn->setEnabled(false);
  emit TcpMgr::GetInstance()
      -> sig_send_data(id, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
