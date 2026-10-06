#ifndef APPLYFRIEND_H
#define APPLYFRIEND_H

#include "common/userdata.h"
#include "dialogs/friendlabel.h"
#include "widgets/clickedlabel.h"
#include <QDialog>
namespace Ui {
class ApplyFriend;
}
class ApplyFriend : public QDialog
{
  Q_OBJECT

public:
  explicit ApplyFriend(QWidget *parent = nullptr);
  ~ApplyFriend();
  void InitTipLbs();
  bool eventFilter(QObject *obj, QEvent *event) override;
  void SetSearchInfo(std::shared_ptr<SearchInfo> si);
  void SetApplyInfo(std::shared_ptr<ApplyInfo> info);
  void SetContactInfo(std::shared_ptr<UserInfo> info);
  void reject() override;

private:
  Ui::ApplyFriend *ui;
  void resetLabels();
  ClickedLabel *createTipLabel(const QString &text);
  void layoutTipLabels();
  void selectLabel(const QString &text);

  // 已经创建好的标签
  QMap<QString, ClickedLabel *> _add_labels;
  std::vector<QString> _add_label_keys;
  // 用来在输入框显示添加新好友的标签
  QMap<QString, FriendLabel *> _friend_labels;
  std::vector<QString> _friend_label_keys;
  void addLabel(QString name);
  std::vector<QString> _tip_data;
  bool _tipsExpanded = false;
  std::shared_ptr<SearchInfo> _si;
  std::shared_ptr<ApplyInfo> _applyInfo;
  bool _pending = false;
  std::shared_ptr<UserInfo> _contactInfo;
  ReqId requestId() const;
public slots:
  // 显示更多label标签
  void ShowMoreLabel();
  // 输入label按下回车触发将标签加入展示栏
  void SlotLabelEnter();
  // 点击关闭，移除展示栏好友便签
  void SlotRemoveFriendLabel(QString);
  // 通过点击tip实现增加和减少好友便签
  void SlotChangeFriendLabelByTip(QString, ClickLbState);
  // 输入框文本变化显示不同提示
  void SlotLabelTextChange(const QString &text);
  // 输入框输入完成
  void SlotLabelEditFinished();
  // 输入标签显示提示框，点击提示框内容后添加好友便签
  void SlotAddFirendLabelByClickTip(QString text);
private slots:
  void on_sure_btn_clicked();
};
#endif // APPLYFRIEND_H
