#include "config.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <utility>

namespace onekvm::oled {

bool displayProfileForVariant(std::string_view variant,
                              std::string_view hardwareRevision,
                              DisplayProfile &output, std::string &error) {
  if (variant == "pcie") {
    output = DisplayProfile{};
    return true;
  }
  if (variant == "cube") {
    output.variant = "cube";
    output.width = kCubeWidth;
    output.height = kCubeHeight;
    output.columnOffset = 0;
    output.pageOffset = 0;
    output.address = 0x3d;
    output.device = !hardwareRevision.empty() &&
                            (hardwareRevision.front() == 'b' ||
                             hardwareRevision.front() == 'B')
                        ? "/dev/i2c-5"
                        : "/dev/i2c-1";
    return true;
  }
  if (variant == "lite") {
    error = "NanoKVM Lite has no OLED display";
    return false;
  }
  error = "NanoKVM variant " + std::string(variant) +
          " has no supported OLED display";
  return false;
}

bool validElementID(std::string_view value) {
  if (value.empty() || value.size() > 32)
    return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char current = value[index];
    const bool alphaNumeric = (current >= 'A' && current <= 'Z') ||
                              (current >= 'a' && current <= 'z') ||
                              (current >= '0' && current <= '9');
    if (!alphaNumeric && (index == 0 || (current != '_' && current != '-')))
      return false;
  }
  return true;
}

bool coordinate(int value) { return value >= 0 && value < kMaxWidth; }

bool validPixelData(std::string_view value, int width, int height) {
  const std::size_t expected =
      (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) + 3) /
      4;
  if (value.size() != expected)
    return false;
  return std::all_of(value.begin(), value.end(), [](char current) {
    return (current >= '0' && current <= '9') ||
           (current >= 'a' && current <= 'f') ||
           (current >= 'A' && current <= 'F');
  });
}

Layout layoutFromLines(std::array<std::string, 4> lines, int rotation,
                       int displayWidth, int displayHeight) {
  const int logicalHeight =
      rotation == 90 || rotation == 270 ? displayWidth : displayHeight;
  const int lineHeight = std::max(1, logicalHeight / 4);
  Layout layout;
  for (int index = 0; index < 4; ++index) {
    Element element;
    element.id = "text" + std::to_string(index + 1);
    element.type = "text";
    element.y = 1 + index * lineHeight;
    element.text = std::move(lines[static_cast<std::size_t>(index)]);
    element.scale = 1;
    element.align = "left";
    layout.elements.push_back(std::move(element));
  }
  return layout;
}

Layout pcieDefaultLayout() {
  Layout layout;

  Element ip;
  ip.id = "text2";
  ip.type = "text";
  ip.name = "IP Address";
  ip.y = 25;
  ip.text = "{ip}";
  ip.scale = 1;
  ip.width = 24;
  ip.fullWidth = true;
  ip.align = "center";
  layout.elements.push_back(std::move(ip));

  Element videoStatus;
  videoStatus.id = "graphic1";
  videoStatus.type = "graphic";
  videoStatus.name = "Video Status";
  videoStatus.x = 3;
  videoStatus.y = 15;
  videoStatus.text = "VIDEO";
  videoStatus.scale = 1;
  videoStatus.fontWidth = 3;
  videoStatus.filled = true;
  videoStatus.mode = "hdmi";
  videoStatus.content = "text";
  layout.elements.push_back(std::move(videoStatus));

  Element live;
  live.id = "live1";
  live.type = "graphic";
  live.name = "LIVE Status";
  live.x = 41;
  live.y = 1;
  live.text = "LIVE";
  live.scale = 1;
  live.fontWidth = 3;
  live.filled = true;
  live.mode = "live";
  live.content = "text";
  layout.elements.push_back(std::move(live));

  Element divider;
  divider.id = "line1";
  divider.type = "line";
  divider.name = "Line";
  divider.y = 23;
  divider.x2 = 63;
  divider.y2 = 23;
  layout.elements.push_back(std::move(divider));

  Element video;
  video.id = "text1";
  video.type = "text";
  video.name = "Video";
  video.y = 9;
  video.text = "{codec} - {fps} FPS";
  video.scale = 1;
  video.fontWidth = 3;
  video.width = 37;
  video.fullWidth = true;
  video.align = "center";
  layout.elements.push_back(std::move(video));

  Element networkStatus;
  networkStatus.id = "graphic2";
  networkStatus.type = "graphic";
  networkStatus.name = "NET Status";
  networkStatus.x = 30;
  networkStatus.y = 15;
  networkStatus.text = "ETH";
  networkStatus.scale = 1;
  networkStatus.fontWidth = 3;
  networkStatus.filled = true;
  networkStatus.mode = "eth";
  networkStatus.content = "text";
  layout.elements.push_back(std::move(networkStatus));

  Element hidStatus;
  hidStatus.id = "graphic3";
  hidStatus.type = "graphic";
  hidStatus.name = "HID Status";
  hidStatus.x = 48;
  hidStatus.y = 15;
  hidStatus.text = "HID";
  hidStatus.scale = 1;
  hidStatus.fontWidth = 3;
  hidStatus.filled = true;
  hidStatus.mode = "hid";
  hidStatus.content = "text";
  layout.elements.push_back(std::move(hidStatus));

  Element brand;
  brand.id = "rect1";
  brand.type = "rect";
  brand.x = 1;
  brand.y = 1;
  brand.width = 40;
  brand.height = 7;
  brand.text = "ONEKVM";
  brand.scale = 1;
  brand.fontWidth = 5;
  brand.align = "center";
  brand.filled = true;
  layout.elements.push_back(std::move(brand));

  return layout;
}

Layout cubeDefaultLayout() {
  auto makeText = [](std::string id, std::string name, int x, int y,
                     std::string value, int fontWidth, int width = 0,
                     bool fullWidth = false,
                     std::string align = "left") {
    Element element;
    element.id = std::move(id);
    element.type = "text";
    element.name = std::move(name);
    element.x = x;
    element.y = y;
    element.width = width;
    element.text = std::move(value);
    element.scale = 1;
    element.fontWidth = fontWidth;
    element.fullWidth = fullWidth;
    element.align = std::move(align);
    return element;
  };
  auto makeStatus = [](std::string id, std::string name, int x, int y,
                       std::string mode, std::string text) {
    Element element;
    element.id = std::move(id);
    element.type = "graphic";
    element.name = std::move(name);
    element.x = x;
    element.y = y;
    element.text = std::move(text);
    element.scale = 1;
    element.fontWidth = 5;
    element.filled = true;
    element.mode = std::move(mode);
    element.content = "text";
    return element;
  };
  auto makeRect = [](std::string id, std::string name, int x, int y,
                     int width, std::string text, int fontWidth) {
    Element element;
    element.id = std::move(id);
    element.type = "rect";
    element.name = std::move(name);
    element.x = x;
    element.y = y;
    element.width = width;
    element.height = 7;
    element.text = std::move(text);
    element.scale = 1;
    element.fontWidth = fontWidth;
    element.align = "center";
    element.filled = true;
    return element;
  };

  Layout layout;
  layout.elements.push_back(
      makeText("text2", "IP", 73, 54, "{ip}", 4));
  Element divider;
  divider.id = "line1";
  divider.type = "line";
  divider.y = 49;
  divider.x2 = 127;
  divider.y2 = 49;
  layout.elements.push_back(std::move(divider));
  layout.elements.push_back(
      makeStatus("graphic1", "NET", 2, 53, "eth", "ETH"));
  layout.elements.push_back(
      makeStatus("graphic2", "HDMI", 23, 53, "hdmi", "HDMI"));
  layout.elements.push_back(
      makeStatus("graphic3", "Live Status", 96, 2, "live", "LIVE"));
  layout.elements.push_back(
      makeStatus("graphic4", "HID", 50, 53, "hid", "HID"));
  layout.elements.push_back(makeText("text3", "Hostname", 0, 42,
                                     "{hostname}", 5, 73, true, "center"));
  layout.elements.push_back(
      makeRect("rect1", "Logo", 3, 2, 39, "ONEKVM", 5));
  layout.elements.push_back(makeText("text1", "Res", 0, 32,
                                     "{resolution} x {fps}HZ", 5, 60, true,
                                     "center"));
  layout.elements.push_back(
      makeRect("rect2", "Codec", 56, 2, 20, "{codec}", 4));
  return layout;
}

bool parseLayout(std::string_view raw, const std::array<std::string, 4> &legacy,
                 int rotation, Layout &output, std::string &error,
                 int displayWidth, int displayHeight,
                 bool useDeviceDefault) {
  if (raw.empty()) {
    if (useDeviceDefault) {
      output = displayWidth == kCubeWidth && displayHeight == kCubeHeight
                   ? cubeDefaultLayout()
                   : pcieDefaultLayout();
    } else {
      output = layoutFromLines(legacy, rotation, displayWidth, displayHeight);
    }
    return true;
  }
  if (raw.size() > 4096) {
    error = "OLED layout exceeds 4096 bytes";
    return false;
  }
  Json document;
  if (!parseJSON(raw, document, error) || document.type != Json::Type::Object) {
    if (error.empty())
      error = "OLED layout must be a JSON object";
    return false;
  }
  if (!jsonInt(document, "version", 0, output.version, error))
    return false;
  if (output.version != 1) {
    error = "unsupported OLED layout version " + std::to_string(output.version);
    return false;
  }
  const Json *elements = document.get("elements");
  if (elements == nullptr || elements->type != Json::Type::Array ||
      elements->array.size() > 32) {
    error = "OLED layout elements must be an array with at most 32 entries";
    return false;
  }
  std::set<std::string> ids;
  output.elements.clear();
  for (const Json &item : elements->array) {
    if (item.type != Json::Type::Object) {
      error = "OLED layout element must be an object";
      return false;
    }
    Element element;
    if (!jsonString(item, "id", {}, element.id, error) ||
        !validElementID(element.id) || !ids.insert(element.id).second) {
      if (error.empty())
        error = "OLED element has invalid or duplicated id";
      return false;
    }
    if (!jsonString(item, "type", {}, element.type, error) ||
        !jsonString(item, "name", {}, element.name, error) ||
        utf8Length(element.name) > 64 ||
        !jsonBool(item, "hidden", false, element.hidden, error) ||
        !jsonInt(item, "x", 0, element.x, error) ||
        !jsonInt(item, "y", 0, element.y, error) || !coordinate(element.x) ||
        !coordinate(element.y)) {
      if (error.empty())
        error = "OLED element coordinates must be between 0 and 127";
      return false;
    }
    if (element.type == "text") {
      if (!jsonString(item, "text", {}, element.text, error) ||
          !jsonInt(item, "scale", 0, element.scale, error) ||
          !jsonInt(item, "font_width", 4, element.fontWidth, error) ||
          !jsonInt(item, "width", 0, element.width, error) ||
          !jsonBool(item, "full_width", false, element.fullWidth, error) ||
          !jsonString(item, "align", "left", element.align, error) ||
          (element.scale != 1 && element.scale != 2) ||
          (element.fontWidth != 3 && element.fontWidth != 4 &&
           element.fontWidth != 5) ||
          element.width < 0 || element.width > kMaxWidth ||
          (element.align != "left" && element.align != "center" &&
           element.align != "right") ||
          utf8Length(element.text) > 128) {
        if (error.empty())
          error = "OLED text element is invalid";
        return false;
      }
    } else if (element.type == "line") {
      if (!jsonInt(item, "x2", 0, element.x2, error) ||
          !jsonInt(item, "y2", 0, element.y2, error) ||
          !coordinate(element.x2) || !coordinate(element.y2)) {
        if (error.empty())
          error = "OLED line coordinates must be between 0 and 127";
        return false;
      }
    } else if (element.type == "rect") {
      if (!jsonInt(item, "width", 0, element.width, error) ||
          !jsonInt(item, "height", 0, element.height, error) ||
          !jsonBool(item, "filled", false, element.filled, error) ||
          !jsonString(item, "text", {}, element.text, error) ||
          !jsonInt(item, "scale", 1, element.scale, error) ||
          !jsonInt(item, "font_width", 4, element.fontWidth, error) ||
          !jsonString(item, "align", "center", element.align, error) ||
          element.width < 1 || element.width > kMaxWidth ||
          element.height < 1 || element.height > kMaxWidth ||
          (element.scale != 1 && element.scale != 2) ||
          (element.fontWidth != 3 && element.fontWidth != 4 &&
           element.fontWidth != 5) ||
          (element.align != "left" && element.align != "center" &&
           element.align != "right") ||
          utf8Length(element.text) > 128) {
        if (error.empty())
          error = "OLED rectangle element is invalid";
        return false;
      }
    } else if (element.type == "animation") {
      if (!jsonString(item, "asset", {}, element.asset, error) ||
          element.asset.size() <= 4 || !element.asset.ends_with(".oan") ||
          element.asset.size() > 96 ||
          !std::all_of(element.asset.begin(), element.asset.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
          }) ||
          !jsonInt(item, "width", 0, element.width, error) ||
          !jsonInt(item, "height", 0, element.height, error) ||
          element.width < 1 || element.width > kMaxWidth ||
          element.height < 1 || element.height > kMaxHeight) {
        if (error.empty())
          error = "OLED animation element is invalid";
        return false;
      }
    } else if (element.type == "graphic") {
      if (!jsonString(item, "mode", "custom", element.mode, error) ||
          (element.mode != "custom" && element.mode != "eth" &&
           element.mode != "hdmi" && element.mode != "hid" &&
           element.mode != "wifi" && element.mode != "live")) {
        if (error.empty())
          error = "OLED graphic element is invalid";
        return false;
      }
      if (element.mode == "custom") {
        if (!jsonInt(item, "width", 0, element.width, error) ||
            !jsonInt(item, "height", 0, element.height, error) ||
            !jsonString(item, "pixels", {}, element.pixels, error) ||
            element.width < 1 || element.width > kMaxWidth ||
            element.height < 1 || element.height > kMaxWidth ||
            !validPixelData(element.pixels, element.width, element.height)) {
          if (error.empty())
            error = "OLED graphic element is invalid";
          return false;
        }
      } else {
        std::string defaultText = "HDMI";
        if (element.mode == "eth")
          defaultText = "ETH";
        else if (element.mode == "hid")
          defaultText = "HID";
        else if (element.mode == "wifi")
          defaultText = "WI-FI";
        else if (element.mode == "live")
          defaultText = "LIVE";
        if (!jsonString(item, "content", "text", element.content, error) ||
            !jsonString(item, "text", defaultText, element.text, error) ||
            !jsonInt(item, "scale", 1, element.scale, error) ||
            !jsonInt(item, "font_width", 4, element.fontWidth, error) ||
            !jsonBool(item, "filled", true, element.filled, error) ||
            !jsonBool(item, "bordered", false, element.bordered, error) ||
            !jsonBool(item, "fixed_size", false, element.fixedSize, error) ||
            (element.content != "text" && element.content != "custom") ||
            (element.scale != 1 && element.scale != 2) ||
            (element.fontWidth != 3 && element.fontWidth != 4 &&
             element.fontWidth != 5) ||
            utf8Length(element.text) > 128) {
          if (error.empty())
            error = "OLED status graphic element is invalid";
          return false;
        }
        if (element.fixedSize &&
            (element.content != "text" ||
             !jsonInt(item, "width", 0, element.width, error) ||
             !jsonInt(item, "height", 0, element.height, error) ||
             element.width < 1 || element.width > kMaxWidth ||
             element.height < 1 || element.height > kMaxWidth)) {
          if (error.empty())
            error = "OLED fixed-size status graphic is invalid";
          return false;
        }
        if (element.content == "custom" &&
            (!jsonInt(item, "width", 0, element.width, error) ||
             !jsonInt(item, "height", 0, element.height, error) ||
             !jsonString(item, "pixels", {}, element.pixels, error) ||
             element.width < 1 || element.width > kMaxWidth ||
             element.height < 1 || element.height > kMaxWidth ||
             !validPixelData(element.pixels, element.width, element.height))) {
          if (error.empty())
            error = "OLED status graphic content is invalid";
          return false;
        }
      }
    } else {
      error = "unsupported OLED element type " + element.type;
      return false;
    }
    output.elements.push_back(std::move(element));
  }
  return true;
}

bool readFile(const std::string &path, std::string &output, std::string &error,
              std::size_t limit) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "open " + path + ": " + std::strerror(errno);
    return false;
  }
  output.assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
  if (stream.bad() || output.size() > limit) {
    error = "read " + path + ": file is too large or unreadable";
    return false;
  }
  return true;
}

bool loadDisplayProfile(const std::string &profilePath,
                        const std::string &hardwarePath,
                        DisplayProfile &output, std::string &error) {
  std::string raw;
  if (!readFile(profilePath, raw, error, 64U << 10))
    return false;
  Json document;
  if (!parseJSON(raw, document, error) || document.type != Json::Type::Object) {
    if (error.empty())
      error = "machine profile must be a JSON object";
    return false;
  }
  std::string machine;
  if (!jsonString(document, "machine", {}, machine, error))
    return false;
  if (machine != "nanokvm") {
    error = "machine " + machine + " has no NanoKVM OLED display";
    return false;
  }

  if (!readFile(hardwarePath, raw, error, 64U << 10))
    return false;
  if (!parseJSON(raw, document, error) || document.type != Json::Type::Object) {
    if (error.empty())
      error = "machine hardware must be a JSON object";
    return false;
  }
  std::string variant;
  std::string hardwareRevision;
  if (!jsonString(document, "variant", {}, variant, error) ||
      !jsonString(document, "revision", {}, hardwareRevision, error))
    return false;
  return displayProfileForVariant(variant, hardwareRevision, output, error);
}

bool validCustomVariableKey(std::string_view key) {
  if (key.size() < 3 || key.size() > 32)
    return false;
  if (key[0] != 'c' || key[1] != '_')
    return false;
  for (std::size_t index = 2; index < key.size(); ++index) {
    const unsigned char current = static_cast<unsigned char>(key[index]);
    if (!std::isalnum(current) && current != '_')
      return false;
  }
  return true;
}

bool parseVariables(const Json &settings,
                    std::map<std::string, std::string> &output,
                    std::string &error) {
  output.clear();
  std::string raw;
  if (!jsonString(settings, "variables_json", "{}", raw, error))
    return false;
  if (raw.empty())
    return true;
  Json document;
  if (!parseJSON(raw, document, error) || document.type != Json::Type::Object) {
    if (error.empty())
      error = "OLED variables must be a JSON object";
    return false;
  }
  if (document.object.size() > 32) {
    error = "OLED allows at most 32 custom variables";
    return false;
  }
  for (const auto &[key, value] : document.object) {
    if (!validCustomVariableKey(key)) {
      error = "OLED custom variable keys must start with c_";
      return false;
    }
    if (value.type != Json::Type::String) {
      error = "OLED custom variable " + key + " must be a string";
      return false;
    }
    if (value.string.size() > 64) {
      error = "OLED custom variable " + key + " is too long";
      return false;
    }
    output[key] = value.string;
  }
  return true;
}

bool loadConfig(const std::string &path, Config &output, std::string &raw,
                std::string &error, int displayWidth,
                int displayHeight) {
  if (!readFile(path, raw, error))
    return false;
  Json settings;
  if (!parseJSON(raw, settings, error) || settings.type != Json::Type::Object) {
    if (error.empty())
      error = "OLED settings must be a JSON object";
    return false;
  }
  const std::set<std::string> allowed = {
      "display_enabled", "burn_in_protection",
      "rotation",        "sleep_seconds",
      "layout_json",     "variables_json",
      "brightness",      "line1",
      "line2",           "line3",
      "line4",
  };
  for (const auto &[key, value] : settings.object) {
    (void)value;
    if (!allowed.contains(key)) {
      error = "unknown OLED setting " + key;
      return false;
    }
  }
  if (!jsonBool(settings, "display_enabled", false, output.enabled, error) ||
      !jsonBool(settings, "burn_in_protection", false, output.burnInProtection,
                error) ||
      !jsonInt(settings, "rotation", 0, output.rotation, error) ||
      !jsonInt(settings, "sleep_seconds", 3600, output.sleepSeconds, error))
    return false;
  if (output.rotation != 0 && output.rotation != 90 && output.rotation != 180 &&
      output.rotation != 270) {
    error = "unsupported OLED rotation " + std::to_string(output.rotation);
    return false;
  }
  if (output.sleepSeconds < 0 || output.sleepSeconds > 86400) {
    error = "OLED sleep time must be between 0 and 86400 seconds";
    return false;
  }
  std::string layoutJSON;
  if (!jsonString(settings, "layout_json", {}, layoutJSON, error))
    return false;
  const bool hasLegacyLines =
      settings.get("line1") != nullptr || settings.get("line2") != nullptr ||
      settings.get("line3") != nullptr || settings.get("line4") != nullptr;
  std::array<std::string, 4> lines =
      hasLegacyLines ? std::array<std::string, 4>{"", "", "", ""}
                     : std::array<std::string, 4>{
                           "ONEKVM", "IP {ip}", "{codec} {fps}FPS", "{stream}"};
  for (int index = 0; index < 4; ++index) {
    const std::string key = "line" + std::to_string(index + 1);
    if (settings.get(key) != nullptr &&
        !jsonString(settings, key, {}, lines[static_cast<std::size_t>(index)],
                    error, true))
      return false;
  }
  const bool useDeviceDefault = !hasLegacyLines;
  if (!parseVariables(settings, output.variables, error))
    return false;
  return parseLayout(layoutJSON, lines, output.rotation, output.layout, error,
                     displayWidth, displayHeight, useDeviceDefault);
}

} // namespace onekvm::oled
