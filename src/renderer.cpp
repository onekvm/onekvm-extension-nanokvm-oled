#include "renderer.hpp"

#include <algorithm>
#include <string>

namespace onekvm::oled {

std::optional<std::pair<int, int>> changedColumns(const Canvas &previous,
                                                  const Canvas &next, int page,
                                                  int width) {
  const auto &before = previous.data();
  const auto &after = next.data();
  const std::size_t offset = static_cast<std::size_t>(page * width);
  int first = 0;
  while (first < width && before[offset + static_cast<std::size_t>(first)] ==
                              after[offset + static_cast<std::size_t>(first)])
    ++first;
  if (first == width)
    return std::nullopt;
  int last = width - 1;
  while (last > first && before[offset + static_cast<std::size_t>(last)] ==
                             after[offset + static_cast<std::size_t>(last)])
    --last;
  return std::pair<int, int>{first, last};
}

void replaceAll(std::string &value, std::string_view from,
                std::string_view to) {
  std::size_t position = 0;
  while ((position = value.find(from, position)) != std::string::npos) {
    value.replace(position, from.size(), to);
    position += to.size();
  }
}

std::string upperASCII(std::string value) {
  for (char &current : value)
    if (current >= 'a' && current <= 'z')
      current = static_cast<char>(current - 'a' + 'A');
  return value;
}

std::string expandTemplate(std::string value, const Status &status) {
  replaceAll(value, "{ip}", status.ip.empty() ? "--" : status.ip);
  replaceAll(value, "{hostname}",
             status.hostname.empty() ? "--" : status.hostname);
  replaceAll(value, "{codec}",
             status.codec.empty() ? "AUTO" : upperASCII(status.codec));
  replaceAll(value, "{fps}", std::to_string(status.actualFPS));
  replaceAll(value, "{resolution}",
             status.inputWidth > 0 && status.inputHeight > 0
                 ? std::to_string(status.inputWidth) + "x" +
                       std::to_string(status.inputHeight)
                 : (status.resolution > 0
                        ? std::to_string(status.resolution) + "P"
                        : "AUTO"));
  replaceAll(value, "{stream}", status.active ? "STREAM ACTIVE" : "KVM READY");
  const int64_t minutes = status.uptime / 60;
  replaceAll(value, "{uptime}",
             minutes >= 60 ? std::to_string(minutes / 60) + "H" +
                                 (minutes % 60 < 10 ? "0" : "") +
                                 std::to_string(minutes % 60) + "M"
                           : std::to_string(minutes) + "M");
  return value;
}

bool statusConnected(std::string_view mode, const Status &status) {
  if (mode == "eth")
    return status.ethernetConnected;
  if (mode == "hdmi")
    return status.hdmiConnected;
  if (mode == "hid")
    return status.hidConnected;
  if (mode == "wifi")
    return status.wifiConnected;
  return mode == "live" && status.active;
}

bool hasLiveLayer(const Config &config) {
  return std::any_of(config.layout.elements.begin(),
                     config.layout.elements.end(), [](const Element &element) {
                       return !element.hidden && element.type == "graphic" &&
                              element.mode == "live";
                     });
}

bool coversDisplay(const Element &element, int displayWidth,
                   int displayHeight) {
  return element.x == 0 && element.y == 0 && element.width == displayWidth &&
         element.height == displayHeight;
}

Canvas render(const Config &config, const Status &status,
              bool liveIndicatorOn, int displayWidth,
              int displayHeight) {
  Canvas canvas(displayWidth, displayHeight);
  const int logicalWidth =
      config.rotation == 90 || config.rotation == 270 ? displayHeight
                                                       : displayWidth;
  bool laterFullscreenAnimation = false;
  for (auto it = config.layout.elements.rbegin();
       it != config.layout.elements.rend(); ++it) {
    if (it->hidden || it->type != "animation")
      continue;
    laterFullscreenAnimation = coversDisplay(*it, displayWidth, displayHeight);
    break;
  }
  for (const Element &element : config.layout.elements) {
    if (element.hidden)
      continue;
    if (laterFullscreenAnimation && element.type != "animation")
      continue;
    if (element.type == "text") {
      const std::string content = expandTemplate(element.text, status);
      const int regionX = element.fullWidth ? 0 : element.x;
      const int regionWidth = element.fullWidth
                                  ? logicalWidth
                                  : (element.width > 0
                                         ? element.width
                                         : textPixelWidth(
                                               content, element.scale,
                                               element.fontWidth));
      const int contentWidth =
          textPixelWidth(content, element.scale, element.fontWidth);
      int textX = regionX;
      if (element.align == "right")
        textX += std::max(0, regionWidth - contentWidth);
      else if (element.align == "center")
        textX += std::max(0, (regionWidth - contentWidth) / 2);
      canvas.text(textX, element.y, content, element.scale, element.fontWidth,
                  config.rotation, true, regionX, regionWidth);
    } else if (element.type == "line")
      canvas.line(element.x, element.y, element.x2, element.y2,
                  config.rotation);
    else if (element.type == "rect") {
      canvas.rectangle(element.x, element.y, element.width, element.height,
                       element.filled, config.rotation);
      if (!element.text.empty() && element.width > 2 && element.height > 2) {
        const std::string content = expandTemplate(element.text, status);
        const int contentWidth =
            textPixelWidth(content, element.scale, element.fontWidth);
        const int contentHeight = textPixelHeight(content, element.scale);
        const int regionX = element.x + 1;
        const int regionY = element.y + 1;
        const int regionWidth = element.width - 2;
        const int regionHeight = element.height - 2;
        int textX = regionX;
        if (element.align == "right")
          textX += std::max(0, regionWidth - contentWidth);
        else if (element.align == "center")
          textX += std::max(0, (regionWidth - contentWidth) / 2);
        const int textY =
            regionY + std::max(0, (regionHeight - contentHeight) / 2);
        canvas.text(textX, textY, content, element.scale, element.fontWidth,
                    config.rotation, !element.filled, regionX, regionWidth,
                    regionY, regionHeight);
      }
    } else if (element.type == "animation") {
      const std::size_t rawBytes = static_cast<std::size_t>(element.width) *
                                   static_cast<std::size_t>(element.height) / 8;
      if (element.pixels.size() == rawBytes)
        canvas.blitPageMajor(element.x, element.y, element.width,
                             element.height, element.pixels, config.rotation);
      else
        canvas.graphic(element.x, element.y, element.width, element.height,
                       element.pixels, config.rotation);
    } else if (element.mode == "custom")
      canvas.graphic(element.x, element.y, element.width, element.height,
                     element.pixels, config.rotation);
    else if (element.mode == "live") {
      constexpr int liveHeight = 7;
      const std::string content = expandTemplate(element.text, status);
      const int liveWidth =
          7 + textPixelWidth(content, 1, element.fontWidth);
      const int renderWidth =
          element.fixedSize ? std::max(element.width, liveWidth) : liveWidth;
      const int renderHeight = element.fixedSize
                                   ? std::max(element.height, liveHeight)
                                   : liveHeight;
      const int contentX = element.x + (renderWidth - liveWidth) / 2;
      const int contentY = element.y + (renderHeight - liveHeight) / 2;
      const bool connected = status.active;
      if (connected)
        canvas.rectangle(element.x, element.y, renderWidth, renderHeight, true,
                         config.rotation);
      const bool contentOn = !connected;
      canvas.text(contentX + 7, contentY + 1, content, 1,
                  element.fontWidth, config.rotation, contentOn, contentX + 7,
                  liveWidth - 7, contentY + 1, 5);
      canvas.graphic(contentX + 1, contentY + 1, 5, 5, "7463170",
                     config.rotation, contentOn);
      if (connected && liveIndicatorOn)
        canvas.rectangle(contentX + 2, contentY + 2, 3, 3, true,
                         config.rotation, false);
    } else {
      const bool connected = statusConnected(element.mode, status);
      const std::string content = expandTemplate(element.text, status);
      const int contentWidth = element.content == "custom"
                                   ? element.width
                                   : textPixelWidth(content, element.scale,
                                                    element.fontWidth);
      const int contentHeight = element.content == "custom"
                                    ? element.height
                                    : textPixelHeight(content, element.scale);
      const bool framed = element.filled || element.bordered;
      const int offset = framed ? 1 : 0;
      const int naturalWidth = contentWidth + (framed ? 2 : 0);
      const int naturalHeight = contentHeight + (framed ? 2 : 0);
      const int renderWidth = element.fixedSize
                                  ? std::max(element.width, naturalWidth)
                                  : naturalWidth;
      const int renderHeight = element.fixedSize
                                   ? std::max(element.height, naturalHeight)
                                   : naturalHeight;
      const int contentX =
          element.x + (renderWidth - naturalWidth) / 2 + offset;
      const int contentY =
          element.y + (renderHeight - naturalHeight) / 2 + offset;
      if (connected && element.filled)
        canvas.rectangle(element.x, element.y, renderWidth, renderHeight, true,
                         config.rotation);
      else if (connected && element.bordered)
        canvas.rectangle(element.x, element.y, renderWidth, renderHeight,
                         false, config.rotation);
      const bool contentOn = !(connected && element.filled);
      if (element.content == "custom")
        canvas.graphic(contentX, contentY, element.width,
                       element.height, element.pixels, config.rotation,
                       contentOn);
      else
        canvas.text(contentX, contentY, content,
                    element.scale, element.fontWidth, config.rotation,
                    contentOn, contentX, contentWidth, contentY,
                    contentHeight);
    }
  }
  return canvas;
}

} // namespace onekvm::oled
