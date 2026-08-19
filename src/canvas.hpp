#pragma once

#include "font.hpp"
#include "types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace onekvm::oled {

static constexpr std::array<std::array<uint8_t, 5>, 26> kLatinFont3{{
    {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3},
    {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
    {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7},
    {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
    {5, 7, 7, 5, 5}, {5, 7, 7, 7, 5}, {2, 5, 5, 5, 2},
    {6, 5, 6, 4, 4}, {2, 5, 5, 3, 1}, {6, 5, 6, 5, 5},
    {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7},
    {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
    {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
}};

static constexpr std::array<std::array<uint8_t, 5>, 26> kLatinFont5{{
    {14, 17, 31, 17, 17}, {30, 17, 30, 17, 30},
    {15, 16, 16, 16, 15}, {30, 17, 17, 17, 30},
    {31, 16, 30, 16, 31}, {31, 16, 30, 16, 16},
    {15, 16, 23, 17, 15}, {17, 17, 31, 17, 17}, {31, 4, 4, 4, 31},
    {1, 1, 1, 17, 14}, {17, 18, 28, 18, 17}, {16, 16, 16, 16, 31},
    {17, 27, 21, 17, 17}, {17, 25, 21, 19, 17},
    {14, 17, 17, 17, 14}, {30, 17, 30, 16, 16},
    {14, 17, 21, 19, 15}, {30, 17, 30, 18, 17}, {15, 16, 14, 1, 30},
    {31, 4, 4, 4, 4}, {17, 17, 17, 17, 14}, {17, 17, 17, 10, 4},
    {17, 17, 21, 27, 17}, {17, 10, 4, 10, 17}, {17, 10, 4, 4, 4},
    {31, 2, 4, 8, 31},
}};

class Canvas {
public:
  explicit Canvas(int width = kPcieWidth, int height = kPcieHeight)
      : width_(width), height_(height) {}

  void set(int x, int y, bool on = true) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_)
      return;
    const std::size_t index = static_cast<std::size_t>((y / 8) * width_ + x);
    const uint8_t mask = static_cast<uint8_t>(1U << (y % 8));
    if (on)
      data_[index] |= mask;
    else
      data_[index] &= static_cast<uint8_t>(~mask);
  }

  bool at(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_)
      return false;
    return (data_[static_cast<std::size_t>((y / 8) * width_ + x)] &
            (1U << (y % 8))) != 0;
  }

  void shift(int dx, int dy) {
    Canvas shifted(width_, height_);
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x)
        if (at(x, y))
          shifted.set(x + dx, y + dy);
    *this = shifted;
  }

  void text(int x, int y, std::string_view value, int scale, int fontWidth,
            int rotation, bool on = true, int clipX = -1, int clipWidth = 0,
            int clipY = -1, int clipHeight = 0) {
    if (scale != 2)
      scale = 1;
    const int logicalWidth =
        rotation == 90 || rotation == 270 ? height_ : width_;
    const int logicalHeight =
        rotation == 90 || rotation == 270 ? width_ : height_;
    std::size_t index = 0;
    uint32_t codepoint = 0;
    while (nextCodepoint(value, index, codepoint)) {
      std::array<uint16_t, 12> chinese{};
      const bool wide = isCJK(codepoint);
      const bool hasChinese = wide && chineseFont.glyph(codepoint, chinese);
      const char character = codepoint < 0x80
                                 ? static_cast<char>(codepoint >= 'a' && codepoint <= 'z'
                                                         ? codepoint - 'a' + 'A'
                                                         : codepoint)
                                 : '?';
      const auto ascii = glyphFor(character, fontWidth);
      const int glyphWidth =
          hasChinese ? 12 : asciiGlyphWidth(character, fontWidth);
      const int glyphHeight = hasChinese ? 12 : 5;
      for (int row = 0; row < glyphHeight; ++row)
        for (int column = 0; column < glyphWidth; ++column) {
          const uint16_t bits = hasChinese ? chinese[static_cast<std::size_t>(row)]
                                           : ascii[static_cast<std::size_t>(row)];
          if ((bits & (1U << (glyphWidth - 1 - column))) == 0)
            continue;
          for (int oy = 0; oy < scale; ++oy)
            for (int ox = 0; ox < scale; ++ox) {
              const int logicalX = x + column * scale + ox;
              const int logicalY = y + row * scale + oy;
              const bool insideHorizontalClip =
                  clipX < 0 ||
                  (logicalX >= clipX && logicalX < clipX + clipWidth);
              const bool insideVerticalClip =
                  clipY < 0 ||
                  (logicalY >= clipY && logicalY < clipY + clipHeight);
              if (insideHorizontalClip && insideVerticalClip && logicalX >= 0 &&
                  logicalX < logicalWidth &&
                  logicalY >= 0 && logicalY < logicalHeight)
                setRotated(logicalX, logicalY, rotation, on);
            }
        }
      x += (wide ? 13 : glyphWidth + 1) * scale;
      if (x >= logicalWidth)
        return;
    }
  }

  void line(int x0, int y0, int x1, int y1, int rotation) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
      setRotated(x0, y0, rotation);
      if (x0 == x1 && y0 == y1)
        return;
      const int twice = 2 * error;
      if (twice >= dy) {
        error += dy;
        x0 += sx;
      }
      if (twice <= dx) {
        error += dx;
        y0 += sy;
      }
    }
  }

  void rectangle(int x, int y, int width, int height, bool filled,
                 int rotation, bool on = true) {
    for (int row = 0; row < height; ++row)
      for (int column = 0; column < width; ++column) {
        if (filled || row == 0 || row == height - 1 || column == 0 ||
            column == width - 1)
          setRotated(x + column, y + row, rotation, on);
      }
  }

  void blitPageMajor(int x, int y, int width, int height,
                     std::string_view bytes, int rotation) {
    const std::size_t frameBytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) / 8;
    if (bytes.size() != frameBytes || width <= 0 || height <= 0 ||
        height % 8 != 0)
      return;
    if (rotation == 0 && y % 8 == 0) {
      const int pages = height / 8;
      for (int page = 0; page < pages; ++page) {
        const int targetPage = y / 8 + page;
        if (targetPage < 0 || targetPage >= height_ / 8)
          continue;
        for (int column = 0; column < width; ++column) {
          const int targetX = x + column;
          if (targetX < 0 || targetX >= width_)
            continue;
          data_[static_cast<std::size_t>(targetPage * width_ + targetX)] =
              static_cast<uint8_t>(
                  bytes[static_cast<std::size_t>(page * width + column)]);
        }
      }
      return;
    }
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        if ((bytes[static_cast<std::size_t>((row >> 3) * width + column)] &
             (1U << (row & 7))) == 0)
          continue;
        setRotated(x + column, y + row, rotation, true);
      }
    }
  }

  void graphic(int x, int y, int width, int height, std::string_view pixels,
               int rotation, bool on = true) {
    for (int row = 0; row < height; ++row)
      for (int column = 0; column < width; ++column) {
        const int index = row * width + column;
        const char encoded = pixels[static_cast<std::size_t>(index / 4)];
        const int nibble = encoded >= '0' && encoded <= '9'
                               ? encoded - '0'
                               : (encoded >= 'a' && encoded <= 'f'
                                      ? encoded - 'a' + 10
                                      : encoded - 'A' + 10);
        if ((nibble & (1 << (3 - index % 4))) != 0)
          setRotated(x + column, y + row, rotation, on);
      }
  }

  const std::array<uint8_t, kMaxFrameBytes> &data() const { return data_; }
  std::array<uint8_t, kMaxFrameBytes> &mutableData() { return data_; }
  bool operator==(const Canvas &other) const {
    return width_ == other.width_ && height_ == other.height_ &&
           data_ == other.data_;
  }

private:
  static std::array<uint8_t, 5> glyphFor(char character, int fontWidth) {
    if (character >= 'A' && character <= 'Z') {
      const auto index = static_cast<std::size_t>(character - 'A');
      if (fontWidth == 3)
        return kLatinFont3[index];
      if (fontWidth == 5)
        return kLatinFont5[index];
    }
    switch (character) {
    case ' ':
      return {0, 0, 0, 0, 0};
    case 'A':
      return {6, 9, 15, 9, 9};
    case 'B':
      return {14, 9, 14, 9, 14};
    case 'C':
      return {7, 8, 8, 8, 7};
    case 'D':
      return {14, 9, 9, 9, 14};
    case 'E':
      return {15, 8, 14, 8, 15};
    case 'F':
      return {15, 8, 14, 8, 8};
    case 'G':
      return {7, 8, 11, 9, 7};
    case 'H':
      return {9, 9, 15, 9, 9};
    case 'I':
      return {15, 2, 2, 2, 15};
    case 'J':
      return {1, 1, 1, 9, 6};
    case 'K':
      return {9, 10, 12, 10, 9};
    case 'L':
      return {8, 8, 8, 8, 15};
    case 'M':
      return {9, 15, 15, 9, 9};
    case 'N':
      return {9, 13, 11, 9, 9};
    case 'O':
      return {6, 9, 9, 9, 6};
    case 'P':
      return {14, 9, 14, 8, 8};
    case 'Q':
      return {6, 9, 9, 11, 7};
    case 'R':
      return {14, 9, 14, 10, 9};
    case 'S':
      return {7, 8, 6, 1, 14};
    case 'T':
      return {15, 2, 2, 2, 2};
    case 'U':
      return {9, 9, 9, 9, 6};
    case 'V':
      return {9, 9, 9, 6, 6};
    case 'W':
      return {9, 9, 15, 15, 9};
    case 'X':
      return {9, 9, 6, 9, 9};
    case 'Y':
      return {9, 9, 6, 2, 2};
    case 'Z':
      return {15, 1, 2, 4, 15};
    case '0':
      return {7, 5, 5, 5, 7};
    case '1':
      return {2, 6, 2, 2, 7};
    case '2':
      return {6, 1, 7, 4, 7};
    case '3':
      return {6, 1, 3, 1, 6};
    case '4':
      return {5, 5, 7, 1, 1};
    case '5':
      return {7, 4, 6, 1, 6};
    case '6':
      return {3, 4, 7, 5, 7};
    case '7':
      return {7, 1, 2, 2, 2};
    case '8':
      return {7, 5, 7, 5, 7};
    case '9':
      return {7, 5, 7, 1, 6};
    case '.':
      return {0, 0, 0, 0, 2};
    case ':':
      return {0, 2, 0, 2, 0};
    case '-':
      return {0, 0, 7, 0, 0};
    case '+':
      return {0, 2, 7, 2, 0};
    case '/':
      return {1, 1, 2, 4, 4};
    default:
      return {6, 1, 2, 0, 2};
    }
  }

  void setRotated(int x, int y, int rotation, bool on = true) {
    if (rotation == 90)
      set(width_ - 1 - y, x, on);
    else if (rotation == 180)
      set(width_ - 1 - x, height_ - 1 - y, on);
    else if (rotation == 270)
      set(y, height_ - 1 - x, on);
    else
      set(x, y, on);
  }

  int width_;
  int height_;
  std::array<uint8_t, kMaxFrameBytes> data_{};
};

} // namespace onekvm::oled
