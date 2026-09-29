#ifndef USERINFOPAGE_H
#define USERINFOPAGE_H

#include <QWidget>
#include <QPixmap>
#include <QByteArray>

namespace Ui {
class UserInfoPage;
}

class UserInfoPage : public QWidget
{
    Q_OBJECT

public:
    explicit UserInfoPage(QWidget *parent = nullptr);
    ~UserInfoPage();
    void Refresh(bool force = false);
signals:
    void sig_profile_changed();
    void sig_logout();

private slots:
    void on_up_btn_clicked();
    void on_submit_btn_clicked();

private:
    Ui::UserInfoPage *ui;
    QPixmap _pendingAvatar;
    QByteArray _pendingAvatarBytes;
    int _savingUid = 0;
    int _displayedUid = 0;
};

#endif // USERINFOPAGE_H
