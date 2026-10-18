#pragma once
#include <memory>
#include <string>
class CSession;
class VoiceCallService {
public:
  static void Handle(const std::shared_ptr<CSession> &session, short id, const std::string &data);
};
