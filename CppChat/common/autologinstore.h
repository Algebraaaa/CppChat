#pragma once
#include <QString>
#include <QUrl>
#include <optional>

namespace AutoLoginStore {
struct Credentials {
  QString email;
  QString password;
};
bool supported();
std::optional<Credentials> load(const QUrl &server);
void save(const QUrl &server, const Credentials &credentials);
void clear(const QUrl &server);
} // namespace AutoLoginStore
