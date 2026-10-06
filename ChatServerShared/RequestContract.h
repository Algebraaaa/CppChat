#pragma once
#include <json/reader.h>
#include <json/value.h>
#include <string>

namespace RequestContract {
inline bool ValidId(const Json::Value &id)
{
  return id.isString() && !id.asString().empty() && id.asString().size() <= 64;
}
// Optional for old clients; present-but-invalid identifiers reject the request.
inline bool ParseRequest(const std::string &text, Json::Value &request)
{
  Json::Reader reader;
  return reader.parse(text, request) && request.isObject() &&
         (!request.isMember("request_id") || ValidId(request["request_id"]));
}
inline void EchoId(const Json::Value &request, Json::Value &response)
{
  if (request.isObject() && ValidId(request["request_id"]))
    response["request_id"] = request["request_id"];
}
} // namespace RequestContract
