#pragma once
#include <optional>
class QApplication;
class QCommandLineParser;

namespace SecurityCommands {
void registerArguments(QCommandLineParser &arguments);
std::optional<int> execute(QApplication &application, const QCommandLineParser &arguments);
} // namespace SecurityCommands
