#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace onekvm::oled {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<Json> array;
  std::map<std::string, Json> object;

  const Json *get(std::string_view key) const {
    if (type != Type::Object)
      return nullptr;
    const auto iterator = object.find(std::string(key));
    return iterator == object.end() ? nullptr : &iterator->second;
  }
};

bool parseJSON(std::string_view input, Json &output, std::string &error);
bool jsonBool(const Json &object, std::string_view key, bool fallback,
              bool &output, std::string &error);
bool jsonInt(const Json &object, std::string_view key, int fallback,
             int &output, std::string &error);
bool jsonString(const Json &object, std::string_view key, std::string fallback,
                std::string &output, std::string &error,
                bool nullable = false);
bool decodeScreenAlert(std::string_view payload, std::string &kind,
                       std::string &title, std::string &body, uint32_t &ttl_ms,
                       std::string &error);
std::size_t utf8Length(std::string_view value);

} // namespace onekvm::oled
