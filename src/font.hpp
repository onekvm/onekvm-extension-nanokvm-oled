#pragma once

#include "config.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace onekvm::oled {

inline uint16_t little16(const char *bytes) {
  return static_cast<uint16_t>(static_cast<unsigned char>(bytes[0])) |
         static_cast<uint16_t>(static_cast<unsigned char>(bytes[1]) << 8U);
}

inline uint32_t little32(const char *bytes) {
  return static_cast<uint32_t>(static_cast<unsigned char>(bytes[0])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(bytes[1])) << 8U) |
         (static_cast<uint32_t>(static_cast<unsigned char>(bytes[2])) << 16U) |
         (static_cast<uint32_t>(static_cast<unsigned char>(bytes[3])) << 24U);
}

class ChineseFont {
public:
  void configure(std::string path) {
    if (path == path_)
      return;
    path_ = std::move(path);
    data_.clear();
    count_ = 0;
    attempted_ = false;
  }

  bool load(const std::string &path, std::string &error) {
    attempted_ = true;
    std::string loaded;
    if (!readFile(path, loaded, error, 1U << 20))
      return false;
    static constexpr std::string_view prefix =
        "globalThis.ONEKVM_CJK_FONT_BASE64=\"";
    if (std::string_view(loaded).starts_with(prefix)) {
      const std::size_t end = loaded.find('"', prefix.size());
      if (end == std::string::npos ||
          !decodeBase64(std::string_view(loaded).substr(prefix.size(),
                                                        end - prefix.size()),
                        loaded, error))
        return false;
    }
    static constexpr std::string_view magic("OKCJK12\0", 8);
    if (loaded.size() < 12 ||
        std::string_view(loaded.data(), magic.size()) != magic) {
      error = "invalid OLED CJK font header";
      return false;
    }
    const uint32_t count = little32(loaded.data() + 8);
    if (loaded.size() != 12 + static_cast<std::size_t>(count) * 28) {
      error = "invalid OLED CJK font length";
      return false;
    }
    uint32_t previous = 0;
    for (uint32_t index = 0; index < count; ++index) {
      const uint32_t current = little32(loaded.data() + 12 + index * 28);
      if (index > 0 && current <= previous) {
        error = "OLED CJK font index is not sorted";
        return false;
      }
      previous = current;
    }
    data_ = std::move(loaded);
    count_ = count;
    return true;
  }

  bool glyph(uint32_t codepoint, std::array<uint16_t, 12> &rows) {
    if (data_.empty()) {
      if (attempted_ || path_.empty())
        return false;
      std::string ignored;
      if (!load(path_, ignored))
        return false;
    }
    uint32_t low = 0;
    uint32_t high = count_;
    while (low < high) {
      const uint32_t middle = low + (high - low) / 2;
      const char *record = data_.data() + 12 + middle * 28;
      const uint32_t current = little32(record);
      if (current < codepoint) {
        low = middle + 1;
      } else if (current > codepoint) {
        high = middle;
      } else {
        for (std::size_t row = 0; row < rows.size(); ++row)
          rows[row] = little16(record + 4 + row * 2);
        return true;
      }
    }
    return false;
  }

private:
  static bool decodeBase64(std::string_view encoded, std::string &output,
                           std::string &error) {
    std::string decoded;
    decoded.reserve(encoded.size() / 4 * 3);
    uint32_t accumulator = 0;
    int bits = 0;
    bool padded = false;
    for (const char current : encoded) {
      if (current == '=') {
        padded = true;
        continue;
      }
      if (padded) {
        error = "invalid OLED CJK font Base64 padding";
        return false;
      }
      int value = -1;
      if (current >= 'A' && current <= 'Z')
        value = current - 'A';
      else if (current >= 'a' && current <= 'z')
        value = current - 'a' + 26;
      else if (current >= '0' && current <= '9')
        value = current - '0' + 52;
      else if (current == '+')
        value = 62;
      else if (current == '/')
        value = 63;
      if (value < 0) {
        error = "invalid OLED CJK font Base64 data";
        return false;
      }
      accumulator = (accumulator << 6U) | static_cast<uint32_t>(value);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        decoded.push_back(static_cast<char>((accumulator >> bits) & 0xffU));
        accumulator &= bits == 0 ? 0U : (1U << bits) - 1U;
      }
    }
    output = std::move(decoded);
    return true;
  }

  std::string data_;
  std::string path_;
  uint32_t count_ = 0;
  bool attempted_ = false;
};

inline ChineseFont chineseFont;

inline bool nextCodepoint(std::string_view value, std::size_t &index,
                   uint32_t &codepoint) {
  if (index >= value.size())
    return false;
  const auto first = static_cast<unsigned char>(value[index++]);
  if (first < 0x80) {
    codepoint = first;
    return true;
  }
  int continuation = 0;
  uint32_t result = 0;
  if ((first & 0xe0) == 0xc0) {
    continuation = 1;
    result = first & 0x1f;
  } else if ((first & 0xf0) == 0xe0) {
    continuation = 2;
    result = first & 0x0f;
  } else if ((first & 0xf8) == 0xf0) {
    continuation = 3;
    result = first & 0x07;
  } else {
    codepoint = '?';
    return true;
  }
  if (index + static_cast<std::size_t>(continuation) > value.size()) {
    codepoint = '?';
    index = value.size();
    return true;
  }
  for (int current = 0; current < continuation; ++current) {
    const auto byte = static_cast<unsigned char>(value[index]);
    if ((byte & 0xc0) != 0x80) {
      codepoint = '?';
      return true;
    }
    ++index;
    result = (result << 6U) | (byte & 0x3fU);
  }
  codepoint = result;
  return true;
}

inline bool isCJK(uint32_t codepoint) {
  return (codepoint >= 0x2e80 && codepoint <= 0x9fff) ||
         (codepoint >= 0xff00 && codepoint <= 0xffef);
}

inline int asciiGlyphWidth(uint32_t codepoint, int fontWidth = 4) {
  if (codepoint >= 'a' && codepoint <= 'z')
    codepoint = codepoint - 'a' + 'A';
  const bool latin = codepoint >= 'A' && codepoint <= 'Z';
  const bool supportedWidth = fontWidth == 3 || fontWidth == 4 || fontWidth == 5;
  return latin && supportedWidth ? fontWidth : 3;
}

inline int textPixelWidth(std::string_view value, int scale, int fontWidth = 4) {
  if (scale != 2)
    scale = 1;
  int width = 0;
  std::size_t index = 0;
  uint32_t codepoint = 0;
  while (nextCodepoint(value, index, codepoint))
    width +=
        (isCJK(codepoint) ? 13 : asciiGlyphWidth(codepoint, fontWidth) + 1) *
        scale;
  return std::max(1, width - scale);
}

inline int textPixelHeight(std::string_view value, int scale) {
  if (scale != 2)
    scale = 1;
  std::size_t index = 0;
  uint32_t codepoint = 0;
  while (nextCodepoint(value, index, codepoint))
    if (isCJK(codepoint))
      return 12 * scale;
  return 5 * scale;
}

} // namespace onekvm::oled
