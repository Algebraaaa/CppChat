#pragma once
#include "data.h"
#include <json/json.h>
#include <memory>
#include <vector>

class MessageService {
public:
  static int Store(int actor,
                   std::vector<std::shared_ptr<ChatMessage>> &messages);
  static void Deliver(int from, int to, int thread,
                      const std::vector<std::shared_ptr<ChatMessage>> &messages,
                      const Json::Value &payload);
  static int CreatePrivate(int uid, int other, int &thread);
  static int Load(int uid, int thread, int cursor,
                  std::shared_ptr<PageResult> &page);
  static bool ListThreads(int uid, int cursor, int limit,
                          std::vector<std::shared_ptr<ChatThreadInfo>> &threads,
                          bool &more, int &next);
};
