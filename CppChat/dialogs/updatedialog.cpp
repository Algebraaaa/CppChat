#include "dialogs/updatedialog.h"
#include "services/clientupdateservice.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(ClientUpdateService *service, QWidget *parent)
    : QDialog(parent), _service(service)
{
  setWindowTitle(tr("CppChat 更新"));
  setObjectName("update_dialog");
  resize(520, 440);
  auto *layout = new QVBoxLayout(this);
  auto *version = new QLabel(tr("当前版本：%1").arg(service->currentVersion()), this);
  layout->addWidget(version);
  _status = new QLabel(this);
  _status->setObjectName("update_status");
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  _notes = new QPlainTextEdit(this);
  _notes->setReadOnly(true);
  _notes->setMinimumHeight(180);
  _notes->setObjectName("update_notes");
  layout->addWidget(_notes);
  _progress = new QProgressBar(this);
  _progress->setRange(0, 1000);
  layout->addWidget(_progress);
  auto *buttons = new QHBoxLayout;
  buttons->addStretch();
  _later = new QPushButton(tr("稍后"), this);
  _later->setAutoDefault(false);
  _action = new QPushButton(this);
  _action->setObjectName("update_action");
  _action->setAutoDefault(false);
  buttons->addWidget(_later);
  buttons->addWidget(_action);
  layout->addLayout(buttons);
  connect(_later, &QPushButton::clicked, this, &UpdateDialog::reject);
  connect(_action, &QPushButton::clicked, this, [this]() {
    switch (_service->state()) {
    case ClientUpdateService::State::Available:
      _service->download();
      break;
    case ClientUpdateService::State::Ready:
      _action->setEnabled(false);
      emit installRequested();
      break;
    default:
      checkManually();
      break;
    }
  });
  connect(service, &ClientUpdateService::stateChanged, this, &UpdateDialog::refresh);
  connect(service, &ClientUpdateService::checkFinished, this, [this](bool available) {
    if (available) {
      show();
      raise();
    } else if (_manual) {
      _status->setText(tr("已经是最新版本"));
    }
  });
  connect(service, &ClientUpdateService::failed, this, [this](const QString &message) {
    qWarning() << "Client update:" << message;
    if (_manual || isVisible())
      _status->setText(message);
  });
  connect(service, &ClientUpdateService::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            _progress->setValue(static_cast<int>(received * 1000 / total));
            _status->setText(tr("正在下载：%1 / %2 MB")
                                 .arg(received / 1048576.0, 0, 'f', 1)
                                 .arg(total / 1048576.0, 0, 'f', 1));
          });
  refresh();
}

void UpdateDialog::checkManually()
{
  _manual = true;
  show();
  raise();
  if (!_service->busy() && _service->state() != ClientUpdateService::State::Ready)
    _service->check();
}

void UpdateDialog::reject()
{
  _manual = false;
  if (_service->busy())
    _service->cancel();
  QDialog::reject();
}

void UpdateDialog::refresh()
{
  using State = ClientUpdateService::State;
  const auto state = _service->state();
  _notes->setVisible(true);
  _notes->setPlainText(_service->releaseNotes());
  _progress->setVisible(state == State::Downloading);
  _action->setEnabled(!_service->busy());
  _later->setText(_service->busy() ? tr("取消") : tr("稍后"));
  if (state == State::Checking) {
    _status->setText(tr("正在检查更新…"));
    _action->setText(tr("检查更新"));
  } else if (state == State::Available) {
    _status->setText(tr("发现新版本 %1").arg(_service->info().version));
    _action->setText(tr("下载更新"));
  } else if (state == State::Downloading) {
    _progress->setValue(0);
    _status->setText(tr("正在下载更新…"));
  } else if (state == State::Ready) {
    _status->setText(tr("下载校验完成，安装时将关闭客户端并保留聊天数据"));
    _action->setText(tr("立即安装"));
  } else {
    _status->setText(tr("点击检查更新以获取最新版本"));
    _action->setText(tr("检查更新"));
  }
}
