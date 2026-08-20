#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace onekvm::oled {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

inline constexpr int kPcieWidth = 64;
inline constexpr int kPcieHeight = 32;
inline constexpr int kCubeWidth = 128;
inline constexpr int kCubeHeight = 64;
inline constexpr int kMaxWidth = kCubeWidth;
inline constexpr int kMaxHeight = kCubeHeight;
inline constexpr int kMaxFrameBytes = kMaxWidth * kMaxHeight / 8;
inline constexpr unsigned long kI2CSlave = 0x0703;
inline constexpr char kRtosDevice[] = "/dev/onekvm-oled";
inline constexpr unsigned long kRtosIoctlBase = 0x4f00;
inline constexpr std::string_view kExtensionID = "nanokvm-oled";
inline constexpr std::string_view kCoreBusName = "org.onekvm.Core1";
inline constexpr std::string_view kStatusPath = "/org/onekvm/Core1/Status";
inline constexpr std::string_view kStatusInterface =
    "org.onekvm.Core1.Status";

struct DisplayProfile {
  std::string variant = "pcie";
  int width = kPcieWidth;
  int height = kPcieHeight;
  int columnOffset = 32;
  int pageOffset = 8;
  int address = 0x3c;
  std::string device = "/dev/i2c-5";
};

struct Status {
  bool active = false;
  bool hdmiConnected = false;
  bool ethernetConnected = false;
  bool hidAvailable = false;
  bool hidConnected = false;
  bool wifiAvailable = false;
  bool wifiConnected = false;
  std::string codec;
  int fps = 0;
  int actualFPS = 0;
  int resolution = 0;
  int inputWidth = 0;
  int inputHeight = 0;
  std::string hostname;
  std::string ip;
  int64_t uptime = 0;
  std::string machine;
  std::string version;
  std::string processor;
  std::string kernel;
  int cpuPercent = -1;
  uint64_t memoryUsed = 0;
  uint64_t memoryTotal = 0;
  uint64_t storageTotal = 0;
  uint64_t storageAvailable = 0;
};

struct Element {
  std::string id;
  std::string type;
  std::string name;
  bool hidden = false;
  int x = 0;
  int y = 0;
  int x2 = 0;
  int y2 = 0;
  int width = 0;
  int height = 0;
  std::string text;
  int scale = 0;
  int fontWidth = 4;
  bool fullWidth = false;
  std::string align = "left";
  bool filled = false;
  bool bordered = false;
  bool fixedSize = false;
  std::string mode;
  std::string content;
  std::string pixels;
  std::string asset;
};

struct Layout {
  int version = 1;
  std::vector<Element> elements;
};

struct Config {
  bool enabled = false;
  bool burnInProtection = false;
  int rotation = 0;
  int sleepSeconds = 0;
  Layout layout;
  std::map<std::string, std::string> variables;
};

} // namespace onekvm::oled
