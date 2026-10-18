#pragma once
#include "common/chatstore.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <stdexcept>

namespace QtFixtures {
inline ChatStoreResult awaitStore(const std::function<void(ChatStore::Callback)> &operation)
{
  struct State {
    bool complete = false;
    bool guiThread = false;
    ChatStoreResult result;
  };
  auto state = std::make_shared<State>();
  QEventLoop loop;
  QPointer<QEventLoop> guard(&loop);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  operation([state, guard](const ChatStoreResult &result) {
    state->result = result;
    state->complete = true;
    state->guiThread = QThread::currentThread() == QCoreApplication::instance()->thread();
    if (guard)
      guard->quit();
  });
  if (!state->complete) {
    deadline.start(8000);
    loop.exec();
  }
  if (!state->complete)
    throw std::runtime_error("ChatStore callback timed out");
  if (!state->guiThread)
    throw std::runtime_error("ChatStore callback left the GUI thread");
  return state->result;
}
} // namespace QtFixtures
