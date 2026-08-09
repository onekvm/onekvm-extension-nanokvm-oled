#include "runtime.hpp"

#include "config.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "status_source.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <signal.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace onekvm::oled {

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
  if (!chineseFont.load(defaultFontPath(), error))
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

bool runDisplay(const Options &options, std::string &error) {
  DisplayProfile profile;
  if (!resolveDisplayProfile(options, profile, error))
    return false;
  if (!chineseFont.load(defaultFontPath(), error))
    return false;
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
  Canvas frame = render(config, status, liveIndicatorOn, profile.width,
                        profile.height);
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
  auto nextShift = Clock::now() + options.shiftInterval;
  auto nextSleep = Clock::now() + 1s;
  auto nextLiveBlink = Clock::now() + 1s;
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
        render(config, status, false, profile.width, profile.height);
    const Canvas on =
        render(config, status, true, profile.width, profile.height);
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
    Canvas nextFrame = render(nextConfigValue, nextStatus, liveIndicatorOn,
                              profile.width, profile.height);
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
      Canvas visible = nextFrame;
      visible.shift(shifts[static_cast<std::size_t>(shiftIndex)].first,
                    shifts[static_cast<std::size_t>(shiftIndex)].second);
      if (!showVisible(visible))
        return false;
      awake = true;
      lastChange = Clock::now();
    }
    config = nextConfigValue;
    status = nextStatus;
    lastFrame = nextFrame;
    if (!config.enabled || !status.active || !hasLiveLayer(config))
      liveFrames.reset();
    else if (settingsChanged || frameChanged || !liveFrames.has_value())
      refreshLiveFrames();
    return true;
  };

  while (!stopped.load()) {
    if (!source.process(error))
      return false;
    Status nextStatus;
    if (source.take(nextStatus)) {
      if (nextStatus.active != status.active)
        liveIndicatorOn = true;
      if (!apply(config, nextStatus, false))
        return false;
    }
    const auto now = Clock::now();
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
    const auto nextWake =
        std::min({nextConfig, nextShift, nextSleep, nextLiveBlink});
    if (!source.waitUntil(nextWake, error))
      return false;
  }
  return !awake || device.off(error);
}

int applicationMain(int argc, char **argv) {
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
  else if (options.off) {
    DisplayProfile profile;
    success = resolveDisplayProfile(options, profile, error);
    if (success) {
      Device device(profile);
      success = openDisplay(device, profile.device, profile.address, 5s,
                            error) &&
                device.off(error);
    }
  } else
    success = runDisplay(options, error);
  if (!success) {
    std::cerr << "onekvm-nanokvm-oled: " << error << '\n';
    return 1;
  }
  return 0;
}

} // namespace onekvm::oled
