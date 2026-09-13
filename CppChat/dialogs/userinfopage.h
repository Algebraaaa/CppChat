#ifndef USERINFOPAGE_H
#define USERINFOPAGE_H

#include <QWidget>

namespace Ui {
class UserInfoPage;
}

class UserInfoPage : public QWidget
{
    Q_OBJECT

public:
    explicit UserInfoPage(QWidget *parent = nullptr);
    ~UserInfoPage();
    void Refresh();
signals:
    void sig_avatar_changed();
    void sig_logout();

private slots:
    void on_up_btn_clicked();

private:
    Ui::UserInfoPage *ui;
};

#endif // USERINFOPAGE_H
