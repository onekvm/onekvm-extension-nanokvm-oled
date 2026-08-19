#include "runtime.hpp"

#include "config.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "status_source.hpp"
#include "assets.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <vector>
#include <signal.h>
#include <sys/resource.h>
#include <string>
#include <string_view>
#include <span>
#include <thread>
#include <unordered_map>
#include <time.h>
#include <unistd.h>
#include <utility>

namespace onekvm::oled {

Clock::time_point monotonicNow() {
  timespec ts{};
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0 &&
      clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return Clock::now();
  return Clock::time_point(std::chrono::seconds(ts.tv_sec) +
                           std::chrono::nanoseconds(ts.tv_nsec));
}

uint64_t waitTimeoutUsec(Clock::time_point now, Clock::time_point deadline) {
  if (deadline <= now)
    return 0;
  const auto timeout =
      std::chrono::duration_cast<std::chrono::microseconds>(deadline - now)
          .count();
  return static_cast<uint64_t>(std::max<int64_t>(1, timeout));
}

std::atomic_bool stopped = false;
extern "C" void stopSignal(int) { stopped.store(true); }

struct Options {
  std::optional<std::string> device;
  std::optional<int> address;
  std::string config;
  std::string profile;
  std::string hardware;
  std::chrono::milliseconds shiftInterval = 60s;
  bool once = false;
  bool health = false;
  bool controller = false;
  bool off = false;
  bool assetsServer = false;
};

std::string defaultConfigPath() {
  const char *directory = std::getenv("ONEKVM_EXTENSION_CONFIG");
  if (directory != nullptr && *directory != '\0')
    return std::string(directory) + "/config.json";
  return "/run/onekvm/extensions/nanokvm-oled/config.json";
}

std::string defaultMachineProfilePath() {
  const char *path = std::getenv("ONEKVM_MACHINE_PROFILE_PATH");
  if (path != nullptr && *path != '\0')
    return path;
  return "/usr/share/onekvm/machine.json";
}

std::string defaultMachineHardwarePath() {
  const char *path = std::getenv("ONEKVM_MACHINE_HARDWARE_PATH");
  if (path != nullptr && *path != '\0')
    return path;
  return "/run/onekvm/hardware.json";
}

bool resolveDisplayProfile(const Options &options, DisplayProfile &profile,
                           std::string &error) {
  if (!loadDisplayProfile(options.profile, options.hardware, profile, error))
    return false;
  if (options.device.has_value())
    profile.device = *options.device;
  if (options.address.has_value())
    profile.address = *options.address;
  return true;
}

std::string defaultFontPath() {
  std::array<char, 4096> path{};
  const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (length <= 0)
    return "fonts/font-cjk-12.bin";
  const std::string executable(path.data(), static_cast<std::size_t>(length));
  const std::size_t slash = executable.find_last_of('/');
  if (slash == std::string::npos)
    return "fonts/font-cjk-12.bin";
  return executable.substr(0, slash) + "/../web/font-cjk-12.js";
}

bool parseInteger(std::string_view value, int &output) {
  if (value.empty())
    return false;
  char *end = nullptr;
  errno = 0;
  const std::string copy(value);
  const long parsed = std::strtol(copy.c_str(), &end, 0);
  if (errno != 0 || end == nullptr || *end != '\0' ||
      parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max())
    return false;
  output = static_cast<int>(parsed);
  return true;
}

bool parseOptions(int argc, char **argv, Options &options, std::string &error) {
  options.config = defaultConfigPath();
  options.profile = defaultMachineProfilePath();
  options.hardware = defaultMachineHardwarePath();
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    auto value = [&](std::string_view name) -> const char * {
      if (index + 1 >= argc) {
        error = std::string(name) + " requires a value";
        return nullptr;
      }
      return argv[++index];
    };
    if (argument == "--device") {
      const char *next = value(argument);
      if (next == nullptr)
        return false;
      options.device = std::string(next);
    } else if (argument == "--address") {
      const char *next = value(argument);
      int address = 0;
      if (next == nullptr || !parseInteger(next, address) || address < 0 ||
          address > 0x7f) {
        error = "invalid OLED address";
        return false;
      }
      options.address = address;
    } else if (argument == "--config") {
      const char *next = value(argument);
      if (next == nullptr)
        return false;
      options.config = next;
    } else if (argument == "--profile") {
      const char *next = value(argument);
      if (next == nullptr)
        return false;
      options.profile = next;
    } else if (argument == "--hardware-profile") {
      const char *next = value(argument);
      if (next == nullptr)
        return false;
      options.hardware = next;
    } else if (argument == "--shift-interval") {
      const char *next = value(argument);
      int seconds = 0;
      if (next == nullptr || !parseInteger(next, seconds) || seconds <= 0) {
        error = "shift interval must be positive seconds";
        return false;
      }
      options.shiftInterval = std::chrono::seconds(seconds);
    } else if (argument == "--once")
      options.once = true;
    else if (argument == "--health")
      options.health = true;
    else if (argument == "--controller")
      options.controller = true;
    else if (argument == "--off")
      options.off = true;
    else if (argument == "--assets-server")
      options.assetsServer = true;
    else {
      error = "unknown argument " + std::string(argument);
      return false;
    }
  }
  return true;
}

bool runHealth(const Options &options, std::string &error) {
  DisplayProfile profile;
  if (!resolveDisplayProfile(options, profile, error))
    return false;
  Config config;
  std::string raw;
  if (!loadConfig(options.config, config, raw, error, profile.width,
                  profile.height))
    return false;
  Device device(profile);
  if (!openDisplay(device, profile.device, profile.address, 2s, error))
    return false;
  device.close();
  StateSource source;
  if (!source.connect(error))
    return false;
  const auto deadline = Clock::now() + 3s;
  while (!source.available() && Clock::now() < deadline) {
    if (!source.process(error) ||
        (!source.available() && !source.waitUntil(deadline, error)))
      return false;
  }
  if (!source.available()) {
    error = "wait for OneKVM D-Bus status timed out";
    return false;
  }
  return true;
}

bool validPlaybackName(std::string_view name) {
  return name.size() > 4 && name.size() <= 96 && name.ends_with(".oan") &&
         std::all_of(name.begin(), name.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
         });
}

bool readPlaybackRequest(const std::filesystem::path &requestPath,
                         std::string &name) {
  std::ifstream file(requestPath);
  if (!file)
    return false;
  std::getline(file, name);
  std::error_code ignored;
  std::filesystem::remove(requestPath, ignored);
  return validPlaybackName(name);
}

uint8_t reverseBits(uint8_t value) {
  value = static_cast<uint8_t>((value >> 4) | (value << 4));
  value = static_cast<uint8_t>(((value & 0xcc) >> 2) | ((value & 0x33) << 2));
  return static_cast<uint8_t>(((value & 0xaa) >> 1) | ((value & 0x55) << 1));
}

bool frameToCanvas(std::span<const unsigned char> bytes, int width, int height,
                   int displayWidth, int displayHeight, int rotation,
                   Canvas &output, std::string &error) {
  const std::size_t frameBytes = static_cast<std::size_t>(width) * height / 8;
  if (bytes.size() != frameBytes) {
    error = "could not read OLED animation";
    return false;
  }
  output = Canvas(displayWidth, displayHeight);
  const int originX = (displayWidth - width) / 2;
  const int originY = (displayHeight - height) / 2;
  // OAN frames use the same page-major byte layout as SSD1306 RAM. For the
  // two common landscape orientations, copy whole pages instead of visiting
  // every pixel and rebuilding the bit mask one pixel at a time.
  if ((rotation == 0 || rotation == 180) && originX >= 0 && originY >= 0 &&
      originX + width <= displayWidth && originY + height <= displayHeight &&
      (originY % 8) == 0) {
    auto &target = output.mutableData();
    const int sourcePages = height / 8;
    if (rotation == 0) {
      for (int page = 0; page < sourcePages; ++page)
        std::copy(bytes.begin() + page * width,
                  bytes.begin() + (page + 1) * width,
                  target.begin() + (originY / 8 + page) * displayWidth + originX);
    } else {
      const int displayPages = displayHeight / 8;
      for (int page = 0; page < sourcePages; ++page) {
        const int targetPage = displayPages - 1 - (originY / 8 + page);
        for (int x = 0; x < width; ++x) {
          const int targetX = displayWidth - 1 - (originX + x);
          target[static_cast<std::size_t>(targetPage * displayWidth + targetX)] =
              reverseBits(bytes[static_cast<std::size_t>(page * width + x)]);
        }
      }
    }
    return true;
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if ((bytes[static_cast<std::size_t>((y >> 3) * width + x)] &
           (1U << (y & 7))) == 0)
        continue;
      const int logicalX = originX + x;
      const int logicalY = originY + y;
      int targetX = logicalX;
      int targetY = logicalY;
      if (rotation == 90) { targetX = displayWidth - 1 - logicalY; targetY = logicalX; }
      else if (rotation == 180) { targetX = displayWidth - 1 - logicalX; targetY = displayHeight - 1 - logicalY; }
      else if (rotation == 270) { targetX = logicalY; targetY = displayHeight - 1 - logicalX; }
      output.set(targetX, targetY);
    }
  }
  return true;
}

bool playAsset(const std::filesystem::path &assetPath, Device &device,
               const DisplayProfile &profile, int rotation, std::string &error) {
  std::ifstream file(assetPath, std::ios::binary);
  std::array<unsigned char, 14> header{};
  file.read(reinterpret_cast<char *>(header.data()), header.size());
  if (!file || std::string_view(reinterpret_cast<char *>(header.data()), 4) != "OAN1") {
    error = "invalid OLED animation";
    return false;
  }
  const int fps = static_cast<int>(header[8] | (header[9] << 8));
  const uint32_t frames = static_cast<uint32_t>(header[10] | (header[11] << 8) |
                                                 (header[12] << 16) | (header[13] << 24));
  const int width = static_cast<int>(header[4] | (header[5] << 8));
  const int height = static_cast<int>(header[6] | (header[7] << 8));
  const std::size_t frameBytes = static_cast<std::size_t>(width) * height / 8;
  if (fps <= 0 || frames == 0 || width < 8 || width > kMaxWidth || height < 8 ||
      height > kMaxHeight || height % 8 != 0) {
    error = "invalid OLED animation header";
    return false;
  }
  std::vector<unsigned char> bytes(frameBytes);
  std::optional<Canvas> previous;
  for (uint32_t frame = 0; frame < frames && !stopped.load(); ++frame) {
    const auto frameStart = Clock::now();
    file.read(reinterpret_cast<char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!file) { error = "could not read OLED animation"; return false; }
    Canvas canvas;
    if (!frameToCanvas(bytes, width, height, profile.width, profile.height,
                       rotation, canvas, error))
      return false;
    if (previous.has_value()) {
      if (!device.showChanged(*previous, canvas, error)) return false;
    } else if (!device.show(canvas, error)) {
      return false;
    }
    previous = canvas;
    const auto elapsed = Clock::now() - frameStart;
    const auto period = std::chrono::milliseconds(1000 / fps);
    if (elapsed < period)
      std::this_thread::sleep_for(period - elapsed);
  }
  return !stopped.load();
}

int deviceRotation(const DisplayProfile &profile, int rotation) {
  /* C906L already programs Cube SSD1306 remap/COM. Do not add another 180°. */
  (void)profile;
  return rotation;
}

struct CachedAnimation {
  int width = 0;
  int height = 0;
  int fps = 0;
  uint32_t frames = 0;
  std::vector<unsigned char> data;
};

bool loadAnimation(const std::filesystem::path &path, CachedAnimation &output) {
  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 14> header{};
  file.read(reinterpret_cast<char *>(header.data()), header.size());
  if (!file ||
      std::string_view(reinterpret_cast<char *>(header.data()), 4) != "OAN1")
    return false;
  output.width = static_cast<int>(header[4] | (header[5] << 8));
  output.height = static_cast<int>(header[6] | (header[7] << 8));
  output.fps = static_cast<int>(header[8] | (header[9] << 8));
  output.frames = static_cast<uint32_t>(header[10] | (header[11] << 8) |
                                        (header[12] << 16) | (header[13] << 24));
  if (output.width < 8 || output.width > kMaxWidth || output.height < 8 ||
      output.height > kMaxHeight || output.height % 8 != 0 || output.fps <= 0 ||
      output.frames == 0)
    return false;
  const std::size_t frameBytes = static_cast<std::size_t>(output.width) *
                                 output.height / 8;
  const std::size_t total = frameBytes * output.frames;
  if (total > 32U * 1024U * 1024U || total / frameBytes != output.frames)
    return false;
  output.data.resize(total);
  file.read(reinterpret_cast<char *>(output.data.data()),
            static_cast<std::streamsize>(total));
  return static_cast<std::size_t>(file.gcount()) == total;
}

void updateAnimationLayers(
    Config &config, const std::filesystem::path &assetRoot,
    Clock::time_point epoch,
    std::unordered_map<std::string, CachedAnimation> &cache) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           Clock::now() - epoch)
                           .count();
  for (Element &element : config.layout.elements) {
    if (element.hidden || element.type != "animation" || element.asset.empty())
      continue;
    auto found = cache.find(element.asset);
    if (found == cache.end()) {
      CachedAnimation animation;
      if (!loadAnimation(assetRoot / element.asset, animation)) {
        cache.emplace(element.asset, CachedAnimation{});
        element.pixels.clear();
        continue;
      }
      found = cache.emplace(element.asset, std::move(animation)).first;
    }
    CachedAnimation &animation = found->second;
    if (animation.frames == 0 || animation.fps <= 0) {
      element.pixels.clear();
      continue;
    }
    const uint64_t frame = static_cast<uint64_t>(std::max<int64_t>(0, elapsed)) *
                           static_cast<uint64_t>(animation.fps) / 1000U %
                           animation.frames;
    const std::size_t frameBytes = static_cast<std::size_t>(animation.width) *
                                   animation.height / 8;
    const std::size_t offset = static_cast<std::size_t>(frame) * frameBytes;
    if (offset + frameBytes > animation.data.size()) {
      element.pixels.clear();
      continue;
    }
    element.width = animation.width;
    element.height = animation.height;
    element.pixels.assign(
        reinterpret_cast<const char *>(animation.data.data() + offset),
        reinterpret_cast<const char *>(animation.data.data() + offset +
                                       frameBytes));
  }
}

bool hasAnimationLayers(const Config &config) {
  return std::any_of(config.layout.elements.begin(), config.layout.elements.end(),
                     [](const Element &element) {
                       return !element.hidden && element.type == "animation" &&
                              !element.asset.empty();
                     });
}

bool runDisplay(const Options &options, std::string &error) {
  DisplayProfile profile;
  if (!resolveDisplayProfile(options, profile, error))
    return false;
  chineseFont.configure(defaultFontPath());
  Config config;
  std::string configRaw;
  if (!loadConfig(options.config, config, configRaw, error, profile.width,
                  profile.height))
    return false;
  StateSource source;
  if (!source.connect(error))
    return false;
  while (!stopped.load() && !source.available()) {
    if (!source.process(error) ||
        (!source.available() &&
         !source.waitUntil(Clock::now() + 1s, error)))
      return false;
  }
  if (stopped.load())
    return true;
  Status status;
  source.take(status);
  Device device(profile);
  if (!openDisplay(device, profile.device, profile.address, 5s, error))
    return false;
  bool liveIndicatorOn = true;
  const std::filesystem::path animationRoot =
      std::getenv("ONEKVM_EXTENSION_DATA") != nullptr
          ? std::filesystem::path(std::getenv("ONEKVM_EXTENSION_DATA")) / "assets"
          : std::filesystem::path{};
  const auto animationEpoch = Clock::now();
  std::unordered_map<std::string, CachedAnimation> animationCache;
  auto renderDisplayFrame = [&](const Config &source, const Status &current,
                                bool live) {
    Config animated = source;
    updateAnimationLayers(animated, animationRoot, animationEpoch,
                          animationCache);
    animated.rotation = deviceRotation(profile, source.rotation);
    return render(animated, current, live, profile.width, profile.height);
  };
  Canvas frame = renderDisplayFrame(config, status, liveIndicatorOn);
  bool awake = config.enabled;
  std::optional<Canvas> displayedFrame;
  if (awake) {
    if (!device.initialize(error) || !device.show(frame, error))
      return false;
    displayedFrame = frame;
  } else if (!device.off(error))
    return false;
  if (options.once)
    return true;

  Canvas lastFrame = frame;
  auto lastChange = Clock::now();
  auto nextConfig = Clock::now() + 500ms;
  auto nextAnimation = config.enabled ? Clock::now() + 33ms
                                       : Clock::time_point::max();
  auto nextShift = Clock::now() + options.shiftInterval;
  auto nextSleep = Clock::now() + 1s;
  auto nextLiveBlink = Clock::now() + 1s;
  const char *extensionData = std::getenv("ONEKVM_EXTENSION_DATA");
  const std::filesystem::path requestPath = extensionData != nullptr
      ? std::filesystem::path(extensionData) / ".play.request"
      : std::filesystem::path{};
  auto nextPlayback = Clock::now() + 100ms;
  int shiftIndex = 0;
  static constexpr std::array<std::pair<int, int>, 4> shifts = {
      {{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  using LiveFrameCache = std::array<std::array<Canvas, 2>, 4>;
  std::optional<LiveFrameCache> liveFrames;
  auto refreshLiveFrames = [&]() {
    if (!config.enabled || !status.active || !hasLiveLayer(config)) {
      liveFrames.reset();
      return;
    }
    const Canvas off =
        renderDisplayFrame(config, status, false);
    const Canvas on =
        renderDisplayFrame(config, status, true);
    LiveFrameCache cache;
    for (std::size_t index = 0; index < shifts.size(); ++index) {
      cache[index][0] = off;
      cache[index][1] = on;
      cache[index][0].shift(shifts[index].first, shifts[index].second);
      cache[index][1].shift(shifts[index].first, shifts[index].second);
    }
    liveFrames = std::move(cache);
  };
  auto showVisible = [&](const Canvas &visible) -> bool {
    if (displayedFrame.has_value()) {
      if (!device.showChanged(*displayedFrame, visible, error))
        return false;
    } else if (!device.show(visible, error)) {
      return false;
    }
    displayedFrame = visible;
    return true;
  };
  refreshLiveFrames();
  auto apply = [&](const Config &nextConfigValue, const Status &nextStatus,
                   bool settingsChanged) -> bool {
    Canvas nextFrame = renderDisplayFrame(nextConfigValue, nextStatus,
                                          liveIndicatorOn);
    const bool frameChanged = !(nextFrame == lastFrame);
    if (!nextConfigValue.enabled) {
      if (awake && !device.off(error))
        return false;
      awake = false;
      displayedFrame.reset();
      shiftIndex = 0;
      config = nextConfigValue;
      status = nextStatus;
      lastFrame = nextFrame;
      liveFrames.reset();
      nextAnimation = Clock::time_point::max();
      return true;
    }
    if (settingsChanged || frameChanged) {
      if (!awake) {
        if (!device.initialize(error))
          return false;
        displayedFrame = Canvas(profile.width, profile.height);
      }
      if (!nextConfigValue.burnInProtection)
        shiftIndex = 0;
      const int dx = shifts[static_cast<std::size_t>(shiftIndex)].first;
      const int dy = shifts[static_cast<std::size_t>(shiftIndex)].second;
      if (dx == 0 && dy == 0) {
        if (!showVisible(nextFrame))
          return false;
      } else {
        Canvas visible = nextFrame;
        visible.shift(dx, dy);
        if (!showVisible(visible))
          return false;
      }
      awake = true;
      lastChange = Clock::now();
    }
    config = nextConfigValue;
    status = nextStatus;
    lastFrame = nextFrame;
    if (!config.enabled || !status.active || !hasLiveLayer(config))
      liveFrames.reset();
    else if (settingsChanged || !liveFrames.has_value() ||
             (frameChanged && !hasAnimationLayers(config)))
      refreshLiveFrames();
    if (config.enabled && nextAnimation == Clock::time_point::max())
      nextAnimation = Clock::now();
    return true;
  };

  while (!stopped.load()) {
    if (!source.process(error))
      return false;
    Status nextStatus;
    if (source.take(nextStatus)) {
      if (nextStatus.active != status.active)
        liveIndicatorOn = true;
      /* Animation already redraws at 30 FPS. Do not let 20 Hz status
       * updates serialize another full frame. */
      if (!hasAnimationLayers(config) ||
          nextStatus.active != status.active) {
        if (!apply(config, nextStatus, false))
          return false;
      } else {
        status = nextStatus;
      }
    }
    const auto now = Clock::now();
    if (!requestPath.empty() && now >= nextPlayback) {
      std::string requested;
      if (readPlaybackRequest(requestPath, requested)) {
        const auto assetPath = std::filesystem::path(extensionData) / "assets" / requested;
        if (std::filesystem::exists(assetPath)) {
          if (!config.enabled && !device.initialize(error)) return false;
          if (!playAsset(assetPath, device, profile,
                         deviceRotation(profile, config.rotation), error))
            return false;
          if (config.enabled) {
            if (!awake) { awake = true; displayedFrame = Canvas(profile.width, profile.height); }
            if (!showVisible(lastFrame)) return false;
          } else {
            if (!device.off(error)) return false;
          }
        }
      }
      nextPlayback = now + 100ms;
    }
    if (now >= nextConfig) {
      Config loaded;
      std::string raw;
      std::string ignored;
      if (loadConfig(options.config, loaded, raw, ignored, profile.width,
                     profile.height) &&
          raw != configRaw) {
        if (!apply(loaded, status, true))
          return false;
        configRaw = std::move(raw);
      }
      nextConfig = now + 500ms;
    }
    if (now >= nextAnimation) {
      if (!apply(config, status, false))
        return false;
      /* ProtectClock coarsens CLOCK_MONOTONIC to 50 ms. Measure the
       * 33 ms period on MONOTONIC_RAW. */
      const auto spinStart = monotonicNow();
      while (monotonicNow() - spinStart < 33ms) {
      }
      nextAnimation = Clock::now();
    }
    if (now >= nextShift) {
      if (awake && config.enabled && config.burnInProtection) {
        shiftIndex = (shiftIndex + 1) % 4;
        Canvas visible = liveFrames.has_value()
                             ? (*liveFrames)[static_cast<std::size_t>(shiftIndex)]
                                            [liveIndicatorOn ? 1U : 0U]
                             : lastFrame;
        if (!liveFrames.has_value())
          visible.shift(shifts[static_cast<std::size_t>(shiftIndex)].first,
                        shifts[static_cast<std::size_t>(shiftIndex)].second);
        if (!showVisible(visible))
          return false;
      }
      nextShift = now + options.shiftInterval;
    }
    if (now >= nextLiveBlink) {
      if (config.enabled && status.active && hasLiveLayer(config)) {
        liveIndicatorOn = !liveIndicatorOn;
        if (!liveFrames.has_value())
          refreshLiveFrames();
        const Canvas &animated =
            (*liveFrames)[0][liveIndicatorOn ? 1U : 0U];
        if (!awake && !device.initialize(error))
          return false;
        if (!awake)
          displayedFrame = Canvas(profile.width, profile.height);
        const Canvas &visible =
            (*liveFrames)[static_cast<std::size_t>(shiftIndex)]
                         [liveIndicatorOn ? 1U : 0U];
        if (!showVisible(visible))
          return false;
        awake = true;
        lastFrame = animated;
        lastChange = now;
      }
      nextLiveBlink = now + 1s;
    }
    if (now >= nextSleep) {
      if (awake && config.sleepSeconds > 0 &&
          now - lastChange >= std::chrono::seconds(config.sleepSeconds)) {
        if (!device.off(error))
          return false;
        awake = false;
        displayedFrame.reset();
      }
      nextSleep = now + 1s;
    }
    const auto nextWake = std::min({nextConfig, nextShift, nextSleep,
                                    nextLiveBlink, nextPlayback, nextAnimation});
    /* ProtectClock makes CLOCK_MONOTONIC nanosleep ~50 ms. poll(2)
     * takes a relative millisecond timeout and stays on 33 ms. */
    if (nextWake != nextAnimation &&
        !source.waitUntil(nextWake, error))
      return false;
  }
  /* C906L owns the panel. Leaving the last frame avoids 0xAE on restart. */
  if (device.usesRtos())
    return true;
  return !awake || device.off(error);
}

int applicationMain(int argc, char **argv) {
  /* 30 FPS animation cannot run at nice 19 under HDMI encode load. */
  (void)::setpriority(PRIO_PROCESS, 0, 5);
  Options options;
  std::string error;
  if (!parseOptions(argc, argv, options, error)) {
    std::cerr << "onekvm-nanokvm-oled: " << error << '\n';
    return 2;
  }
  if (options.controller) {
    const std::string input(std::istreambuf_iterator<char>(std::cin),
                            std::istreambuf_iterator<char>());
    std::string output;
    if (!controllerPlan(input, output, error)) {
      std::cerr << "onekvm-nanokvm-oled: " << error << '\n';
      return 1;
    }
    std::cout << output << '\n';
    return 0;
  }
  struct sigaction action{};
  action.sa_handler = stopSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  bool success = false;
  if (options.health)
    success = runHealth(options, error);
  else if (options.assetsServer) {
    const char *socket = std::getenv("ONEKVM_EXTENSION_SOCKET");
    const char *data = std::getenv("ONEKVM_EXTENSION_DATA");
    success = socket != nullptr && data != nullptr && runAssetServer(
        std::string(socket) + "/assets.sock", std::string(data) + "/assets", error);
  } else if (options.off) {
    DisplayProfile profile;
    success = resolveDisplayProfile(options, profile, error);
    if (success) {
      Device device(profile);
      success = openDisplay(device, profile.device, profile.address, 5s,
                            error) &&
                device.off(error);
    }
  } else {
    const char *socket = std::getenv("ONEKVM_EXTENSION_SOCKET");
    const char *data = std::getenv("ONEKVM_EXTENSION_DATA");
    if (socket != nullptr && data != nullptr) {
      std::thread([socketPath = std::string(socket), dataPath = std::string(data)] {
        std::string ignored;
        (void)runAssetServer(socketPath + "/assets.sock", dataPath + "/assets", ignored);
      }).detach();
    }
    success = runDisplay(options, error);
  }
  if (!success) {
    std::cerr << "onekvm-nanokvm-oled: " << error << '\n';
    return 1;
  }
  return 0;
}

} // namespace onekvm::oled
