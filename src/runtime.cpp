#include "runtime.hpp"

#include "config.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "status_source.hpp"
#include "assets.hpp"
#include "oan.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
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
#include <poll.h>

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

bool showPixels(Device &device, const DisplayProfile &profile, int rotation,
                std::span<const unsigned char> bytes, int width, int height,
                std::optional<Canvas> &previous, std::string &error) {
  Canvas canvas;
  if (!frameToCanvas(bytes, width, height, profile.width, profile.height,
                     rotation, canvas, error))
    return false;
  if (previous.has_value()) {
    if (!device.showChanged(*previous, canvas, error))
      return false;
  } else if (!device.show(canvas, error)) {
    return false;
  }
  previous = std::move(canvas);
  return true;
}

void waitDelay(Clock::time_point started, uint16_t delay_ms) {
  const auto elapsed = Clock::now() - started;
  const auto period = std::chrono::milliseconds(delay_ms);
  if (elapsed < period)
    std::this_thread::sleep_for(period - elapsed);
}

bool playAsset(const std::filesystem::path &assetPath, Device &device,
               const DisplayProfile &profile, int rotation, std::string &error) {
  std::error_code fileError;
  const auto size = std::filesystem::file_size(assetPath, fileError);
  std::ifstream file(assetPath, std::ios::binary);
  OanInfo info{};
  std::vector<unsigned char> pixels;
  std::vector<OanEvent> events;
  if (fileError || !loadOan(file, size, info, pixels, events)) {
    error = "invalid OLED animation";
    return false;
  }
  if (info.width > kMaxWidth || info.height > kMaxHeight) {
    error = "invalid OLED animation header";
    return false;
  }
  std::vector<unsigned char> buf(info.frame_bytes, 0);
  std::optional<Canvas> previous;
  bool have = false;
  uint32_t pc = 0;
  unsigned guard = 0;
  while (pc < events.size() && !stopped.load() &&
         guard++ < 16U * 1024U * 1024U) {
    const OanEvent &event = events[pc];
    if (event.op == kOanOpLoop) {
      if (event.arg >= events.size())
        break;
      pc = event.arg;
      continue;
    }
    const auto frameStart = Clock::now();
    if (event.op == kOanOpFrame) {
      if (event.frame_index < 0)
        break;
      const std::size_t offset =
          static_cast<std::size_t>(event.frame_index) * info.frame_bytes;
      if (offset + info.frame_bytes > pixels.size()) {
        error = "could not read OLED animation";
        return false;
      }
      std::memcpy(buf.data(), pixels.data() + offset, info.frame_bytes);
      have = true;
      if (!showPixels(device, profile, rotation,
                      std::span<const unsigned char>(buf.data(), info.frame_bytes),
                      info.width, info.height, previous, error))
        return false;
    } else if (event.op == kOanOpInvertX && have) {
      oanInvertX(buf.data(), info.width, info.height, event.arg);
      if (!showPixels(device, profile, rotation,
                      std::span<const unsigned char>(buf.data(), info.frame_bytes),
                      info.width, info.height, previous, error))
        return false;
    } else if (event.op == kOanOpInvertY && have) {
      oanInvertY(buf.data(), info.width, info.height, event.arg);
      if (!showPixels(device, profile, rotation,
                      std::span<const unsigned char>(buf.data(), info.frame_bytes),
                      info.width, info.height, previous, error))
        return false;
    } else if (event.op != kOanOpSleep) {
      error = "invalid OLED animation";
      return false;
    }
    waitDelay(frameStart, event.delay_ms);
    ++pc;
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
  uint32_t duration_ms = 0;
  uint32_t loop_index = kOanNoLoop;
  std::size_t frame_bytes = 0;
  std::vector<unsigned char> data;
  std::vector<OanStep> steps;
};

bool loadAnimation(const std::filesystem::path &path, CachedAnimation &output) {
  std::error_code fileError;
  const auto size = std::filesystem::file_size(path, fileError);
  std::ifstream file(path, std::ios::binary);
  OanInfo info{};
  std::vector<unsigned char> source;
  std::vector<OanEvent> events;
  if (fileError || !loadOan(file, size, info, source, events))
    return false;
  if (info.width > kMaxWidth || info.height > kMaxHeight)
    return false;
  if (!expandOanRaster(info, source, events, output.data, output.steps,
                       output.loop_index))
    return false;
  output.width = info.width;
  output.height = info.height;
  output.duration_ms = oanRasterDuration(output.steps);
  output.frame_bytes = info.frame_bytes;
  return true;
}

void updateAnimationLayers(
    Config &config, const std::filesystem::path &assetRoot, uint64_t tick,
    std::unordered_map<std::string, CachedAnimation> &cache) {
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
    if (animation.steps.empty() || animation.duration_ms == 0 ||
        animation.frame_bytes == 0) {
      element.pixels.clear();
      continue;
    }
    /* Index from the 30 Hz apply tick, not CLOCK_MONOTONIC. A 50 ms
     * coarse clock repeats the same OAN frame and drops seq to 20/s. */
    const uint64_t elapsed_ms = tick * 1000U / 30U;
    const int frame = oanFrameAt(animation.steps, animation.duration_ms,
                                 animation.loop_index, elapsed_ms);
    if (frame < 0) {
      element.pixels.clear();
      continue;
    }
    const std::size_t frameBytes = animation.frame_bytes;
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
  uint64_t animationTick = 0;
  std::unordered_map<std::string, CachedAnimation> animationCache;
  auto renderDisplayFrame = [&](const Config &source, const Status &current,
                                bool live) {
    Config animated = source;
    updateAnimationLayers(animated, animationRoot, animationTick,
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
  std::optional<OledAlert> currentAlert;
  auto alertDeadline = Clock::time_point::max();
  auto displayConfig = [&](const Config &base) {
    if (!currentAlert.has_value())
      return base;
    Config overlay = base;
    overlay.enabled = true;
    overlay.layout = layoutForScreenAlert(
        *currentAlert, overlay.rotation, profile.width, profile.height);
    return overlay;
  };
  auto apply = [&](const Config &nextConfigValue, const Status &nextStatus,
                   bool settingsChanged, bool forceShow = false) -> bool {
    const Config visibleConfig = displayConfig(nextConfigValue);
    Canvas nextFrame = renderDisplayFrame(visibleConfig, nextStatus,
                                          liveIndicatorOn);
    const bool frameChanged = forceShow || !(nextFrame == lastFrame);
    if (!visibleConfig.enabled) {
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
      if (!visibleConfig.burnInProtection)
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
    OledAlert nextAlert;
    if (source.takeAlert(nextAlert)) {
      currentAlert = std::move(nextAlert);
      if (currentAlert->ttl_ms == 0)
        alertDeadline = Clock::time_point::max();
      else
        alertDeadline = Clock::now() +
                        std::chrono::milliseconds(currentAlert->ttl_ms);
      if (!apply(config, status, true, true))
        return false;
    }
    std::string dismissedId;
    if (source.takeDismiss(dismissedId)) {
      if (currentAlert.has_value() &&
          (dismissedId.empty() || dismissedId == currentAlert->id)) {
        currentAlert.reset();
        alertDeadline = Clock::time_point::max();
        if (!apply(config, status, true, true))
          return false;
      }
    }
    if (currentAlert.has_value() && Clock::now() >= alertDeadline) {
      currentAlert.reset();
      alertDeadline = Clock::time_point::max();
      if (!apply(config, status, true, true))
        return false;
    }
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
      if (currentAlert.has_value()) {
        nextAnimation = Clock::now() + 33ms;
      } else {
      /* Period is 33 ms including render+show. Do not sleep another 33 ms
       * after apply, and do not use D-Bus poll for the remainder: a
       * readable bus fd would cut the wait, a 20 Hz signal would stretch
       * it. poll(0, timeout) is a relative millisecond sleep. */
      animationTick++;
      const auto frameStart = monotonicNow();
      if (!apply(config, status, false, true))
        return false;
      constexpr auto kAnimationPeriod = 33ms;
      const auto elapsed = monotonicNow() - frameStart;
      if (elapsed < kAnimationPeriod) {
        const int timeoutMs = static_cast<int>(std::chrono::duration_cast<
            std::chrono::milliseconds>(kAnimationPeriod - elapsed)
                                                   .count());
        if (timeoutMs > 0)
          (void)::poll(nullptr, 0, timeoutMs);
      }
      nextAnimation = Clock::now();
      }
    }
    if (now >= nextShift) {
      if (!currentAlert.has_value() && awake && config.enabled &&
          config.burnInProtection) {
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
      if (!currentAlert.has_value() && config.enabled && status.active &&
          hasLiveLayer(config)) {
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
      if (awake && config.sleepSeconds > 0 && !currentAlert.has_value() &&
          now - lastChange >= std::chrono::seconds(config.sleepSeconds)) {
        if (!device.off(error))
          return false;
        awake = false;
        displayedFrame.reset();
      }
      nextSleep = now + 1s;
    }
    const auto nextWake = std::min({nextConfig, nextShift, nextSleep,
                                    nextLiveBlink, nextPlayback, nextAnimation,
                                    alertDeadline});
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
