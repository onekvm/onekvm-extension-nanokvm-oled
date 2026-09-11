#include "json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

namespace onekvm::oled {

class JsonParser {
public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  bool parse(Json &output, std::string &error) {
    skipSpace();
    if (!parseValue(output, 0)) {
      error = error_ + " at byte " + std::to_string(position_);
      return false;
    }
    skipSpace();
    if (position_ != input_.size()) {
      error = "trailing JSON at byte " + std::to_string(position_);
      return false;
    }
    return true;
  }

private:
  bool parseValue(Json &output, int depth) {
    if (depth > 64)
      return fail("JSON nesting is too deep");
    skipSpace();
    if (position_ >= input_.size())
      return fail("unexpected end of JSON");
    const char current = input_[position_];
    if (current == 'n')
      return literal("null", Json::Type::Null, output);
    if (current == 't') {
      if (!literal("true", Json::Type::Bool, output))
        return false;
      output.boolean = true;
      return true;
    }
    if (current == 'f') {
      if (!literal("false", Json::Type::Bool, output))
        return false;
      output.boolean = false;
      return true;
    }
    if (current == '"') {
      output.type = Json::Type::String;
      return parseString(output.string);
    }
    if (current == '[')
      return parseArray(output, depth + 1);
    if (current == '{')
      return parseObject(output, depth + 1);
    if (current == '-' || (current >= '0' && current <= '9'))
      return parseNumber(output);
    return fail("unexpected JSON token");
  }

  bool literal(std::string_view value, Json::Type type, Json &output) {
    if (input_.substr(position_, value.size()) != value)
      return fail("invalid JSON literal");
    position_ += value.size();
    output.type = type;
    return true;
  }

  bool parseString(std::string &output) {
    if (input_[position_++] != '"')
      return fail("expected JSON string");
    output.clear();
    while (position_ < input_.size()) {
      unsigned char current = static_cast<unsigned char>(input_[position_++]);
      if (current == '"')
        return true;
      if (current < 0x20)
        return fail("control byte in JSON string");
      if (current != '\\') {
        output.push_back(static_cast<char>(current));
        continue;
      }
      if (position_ >= input_.size())
        return fail("unfinished JSON escape");
      const char escaped = input_[position_++];
      switch (escaped) {
      case '"':
        output.push_back('"');
        break;
      case '\\':
        output.push_back('\\');
        break;
      case '/':
        output.push_back('/');
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u': {
        uint32_t codepoint = 0;
        if (!hex4(codepoint))
          return false;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
          if (position_ + 2 > input_.size() ||
              input_.substr(position_, 2) != "\\u")
            return fail("missing low surrogate");
          position_ += 2;
          uint32_t low = 0;
          if (!hex4(low) || low < 0xdc00 || low > 0xdfff)
            return fail("invalid low surrogate");
          codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
          return fail("unexpected low surrogate");
        }
        appendUTF8(output, codepoint);
        break;
      }
      default:
        return fail("invalid JSON escape");
      }
    }
    return fail("unterminated JSON string");
  }

  bool hex4(uint32_t &value) {
    if (position_ + 4 > input_.size())
      return fail("short Unicode escape");
    value = 0;
    for (int index = 0; index < 4; ++index) {
      const char current = input_[position_++];
      value <<= 4;
      if (current >= '0' && current <= '9')
        value |= static_cast<uint32_t>(current - '0');
      else if (current >= 'a' && current <= 'f')
        value |= static_cast<uint32_t>(current - 'a' + 10);
      else if (current >= 'A' && current <= 'F')
        value |= static_cast<uint32_t>(current - 'A' + 10);
      else
        return fail("invalid Unicode escape");
    }
    return true;
  }

  static void appendUTF8(std::string &output, uint32_t codepoint) {
    if (codepoint <= 0x7f)
      output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  bool parseArray(Json &output, int depth) {
    ++position_;
    output.type = Json::Type::Array;
    skipSpace();
    if (consume(']'))
      return true;
    for (;;) {
      Json child;
      if (!parseValue(child, depth))
        return false;
      output.array.push_back(std::move(child));
      skipSpace();
      if (consume(']'))
        return true;
      if (!consume(','))
        return fail("expected comma in JSON array");
    }
  }

  bool parseObject(Json &output, int depth) {
    ++position_;
    output.type = Json::Type::Object;
    skipSpace();
    if (consume('}'))
      return true;
    for (;;) {
      skipSpace();
      if (position_ >= input_.size() || input_[position_] != '"')
        return fail("expected JSON object key");
      std::string key;
      if (!parseString(key))
        return false;
      skipSpace();
      if (!consume(':'))
        return fail("expected colon after JSON object key");
      Json value;
      if (!parseValue(value, depth))
        return false;
      if (!output.object.emplace(std::move(key), std::move(value)).second)
        return fail("duplicate JSON object key");
      skipSpace();
      if (consume('}'))
        return true;
      if (!consume(','))
        return fail("expected comma in JSON object");
    }
  }

  bool parseNumber(Json &output) {
    const std::size_t start = position_;
    if (consume('-') && position_ >= input_.size())
      return fail("short JSON number");
    if (consume('0')) {
      if (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9')
        return fail("leading zero in JSON number");
    } else {
      if (!digits())
        return fail("invalid JSON number");
    }
    if (consume('.')) {
      if (!digits())
        return fail("missing JSON fractional digits");
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-'))
        ++position_;
      if (!digits())
        return fail("missing JSON exponent digits");
    }
    const std::string numberText(input_.substr(start, position_ - start));
    char *end = nullptr;
    errno = 0;
    const double number = std::strtod(numberText.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(number))
      return fail("invalid JSON number");
    output.type = Json::Type::Number;
    output.number = number;
    return true;
  }

  bool digits() {
    const std::size_t start = position_;
    while (position_ < input_.size() && input_[position_] >= '0' &&
           input_[position_] <= '9')
      ++position_;
    return position_ != start;
  }

  bool consume(char expected) {
    if (position_ >= input_.size() || input_[position_] != expected)
      return false;
    ++position_;
    return true;
  }

  void skipSpace() {
    while (position_ < input_.size()) {
      const char current = input_[position_];
      if (current != ' ' && current != '\t' && current != '\r' &&
          current != '\n')
        break;
      ++position_;
    }
  }

  bool fail(std::string message) {
    error_ = std::move(message);
    return false;
  }

  std::string_view input_;
  std::size_t position_ = 0;
  std::string error_;
};

bool parseJSON(std::string_view input, Json &output, std::string &error) {
  return JsonParser(input).parse(output, error);
}

bool jsonBool(const Json &object, std::string_view key, bool fallback,
              bool &output, std::string &error) {
  const Json *value = object.get(key);
  if (value == nullptr) {
    output = fallback;
    return true;
  }
  if (value->type != Json::Type::Bool) {
    error = std::string(key) + " must be boolean";
    return false;
  }
  output = value->boolean;
  return true;
}

bool jsonInt(const Json &object, std::string_view key, int fallback,
             int &output, std::string &error) {
  const Json *value = object.get(key);
  if (value == nullptr) {
    output = fallback;
    return true;
  }
  if (value->type != Json::Type::Number ||
      std::floor(value->number) != value->number ||
      value->number < std::numeric_limits<int>::min() ||
      value->number > std::numeric_limits<int>::max()) {
    error = std::string(key) + " must be an integer";
    return false;
  }
  output = static_cast<int>(value->number);
  return true;
}

bool jsonString(const Json &object, std::string_view key, std::string fallback,
                std::string &output, std::string &error,
                bool nullable) {
  const Json *value = object.get(key);
  if (value == nullptr || (nullable && value->type == Json::Type::Null)) {
    output = std::move(fallback);
    return true;
  }
  if (value->type != Json::Type::String) {
    error = std::string(key) + " must be a string";
    return false;
  }
  output = value->string;
  return true;
}

bool decodeScreenAlert(std::string_view payload, std::string &kind,
                       std::string &title, std::string &body, uint32_t &ttl_ms,
                       std::string &error) {
  Json document;
  if (!parseJSON(payload, document, error) ||
      document.type != Json::Type::Object) {
    if (error.empty())
      error = "screen alert JSON must be an object";
    return false;
  }
  std::string ignored;
  if (!jsonString(document, "kind", {}, kind, ignored))
    kind.clear();
  if (!jsonString(document, "title", {}, title, error))
    title.clear();
  if (!jsonString(document, "body", {}, body, error))
    body.clear();
  int ttl = 0;
  if (!jsonInt(document, "ttl_ms", 0, ttl, error) || ttl < 0)
    ttl = 0;
  ttl_ms = static_cast<uint32_t>(ttl);
  if (kind.empty() && title.empty() && body.empty()) {
    error = "screen alert is empty";
    return false;
  }
  error.clear();
  return true;
}

std::size_t utf8Length(std::string_view value) {
  std::size_t count = 0;
  for (unsigned char byte : value)
    if ((byte & 0xc0) != 0x80)
      ++count;
  return count;
}

} // namespace onekvm::oled
