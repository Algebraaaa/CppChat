#pragma once
#include <algorithm>
#include <cctype>
#include <json/json.h>
#include <string>

// Tags belong to the owner of a friendship row, not the other user's profile.
inline bool NormalizeContactTags(const Json::Value &input, Json::Value &tags) {
  tags = Json::Value(Json::arrayValue);
  if (!input.isArray() || input.size() > 12)
    return false;
  for (const auto &item : input) {
    if (!item.isString())
      return false;
    std::string text = item.asString();
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
      return false;
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    if (text.size() > 128)
      return false;
    if (std::any_of(text.begin(), text.end(),
                    [](unsigned char c) { return c < 32; }))
      return false;
    bool duplicate = false;
    for (const auto &existing : tags)
      if (existing.asString() == text)
        duplicate = true;
    if (!duplicate)
      tags.append(text);
  }
  return true;
}

inline std::string SerializeContactTags(const Json::Value &tags) {
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return Json::writeString(writer, tags);
}

inline Json::Value ParseContactTags(const std::string &text) {
  Json::Reader reader;
  Json::Value input, result(Json::arrayValue);
  if (reader.parse(text, input))
    NormalizeContactTags(input, result);
  return result;
}
