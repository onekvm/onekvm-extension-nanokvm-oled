#pragma once

#include "renderer.hpp"
#include "types.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace onekvm::oled {

class Device {
public:
  explicit Device(DisplayProfile profile) : profile_(std::move(profile)) {}
  ~Device() { close(); }
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;

  bool open(const std::string &path, int address, std::string &error) {
    close();
    descriptor_ = ::open(kRtosDevice, O_RDWR | O_CLOEXEC);
    if (descriptor_ >= 0) {
      rtos_ = true;
      return true;
    }
    rtos_ = false;
    if (::access("/run/onekvm/oled-rtos", R_OK) == 0) {
      error = "C906L owns the OLED; /dev/onekvm-oled is not available";
      return false;
    }
    descriptor_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (descriptor_ < 0) {
      error = "open OLED bus " + path + ": " + std::strerror(errno);
      return false;
    }
    if (::ioctl(descriptor_, kI2CSlave, address) < 0) {
      error = "select OLED address on " + path + ": " + std::strerror(errno);
      close();
      return false;
    }
    return true;
  }

  bool initialize(std::string &error) {
    if (rtos_)
      return true;
    static constexpr std::array<uint8_t, 30> sequence = {
        0xae, 0x00, 0x10, 0x40, 0x81, 0xcf, 0xa1, 0xc8, 0xa6, 0xa8,
        0x3f, 0xd3, 0x00, 0xd5, 0x80, 0xd9, 0xf1, 0xda, 0x12, 0xdb,
        0x40, 0x20, 0x02, 0x8d, 0x14, 0xa4, 0xa6, 0xaf, 0xa0, 0xc0,
    };
    if (!writeBytes(0x00, sequence.data(), sequence.size(), error))
      return false;
    Canvas blank(profile_.width, profile_.height);
    return show(blank, error);
  }

  bool show(const Canvas &canvas, std::string &error) {
    if (rtos_)
      return writeRtosFrame(canvas, error);
    const int pages = profile_.height / 8;
    for (int page = 0; page < pages; ++page) {
      const uint8_t position[] = {
          static_cast<uint8_t>(0xb0 + profile_.pageOffset + page),
          static_cast<uint8_t>(0x10 | ((profile_.columnOffset >> 4) & 0x0f)),
          static_cast<uint8_t>(profile_.columnOffset & 0x0f)};
      if (!writeBytes(0x00, position, sizeof(position), error))
        return false;
      const auto &frame = canvas.data();
      if (!writeBytes(0x40, frame.data() + page * profile_.width,
                      static_cast<std::size_t>(profile_.width), error))
        return false;
    }
    return true;
  }

  bool showChanged(const Canvas &previous, const Canvas &next,
                   std::string &error) {
    if (previous == next)
      return true;
    if (rtos_)
      return writeRtosFrame(next, error);
    const int pages = profile_.height / 8;
    for (int page = 0; page < pages; ++page) {
      const auto changed = changedColumns(previous, next, page, profile_.width);
      if (!changed.has_value())
        continue;
      const int first = changed->first;
      const int count = changed->second - first + 1;
      const int column = profile_.columnOffset + first;
      const uint8_t position[] = {
          static_cast<uint8_t>(0xb0 + profile_.pageOffset + page),
          static_cast<uint8_t>(0x10 | ((column >> 4) & 0x0f)),
          static_cast<uint8_t>(column & 0x0f)};
      if (!writeBytes(0x00, position, sizeof(position), error))
        return false;
      const auto &frame = next.data();
      if (!writeBytes(0x40,
                      frame.data() + page * profile_.width + first,
                      static_cast<std::size_t>(count), error))
        return false;
    }
    return true;
  }

  bool usesRtos() const { return rtos_; }

  bool off(std::string &error) {
    if (rtos_) {
      if (::ioctl(descriptor_, _IO('O', 5), 0) < 0) {
        error = "OLED off: " + std::string(std::strerror(errno));
        return false;
      }
      return true;
    }
    Canvas blank(profile_.width, profile_.height);
    if (!show(blank, error))
      return false;
    const uint8_t commands[] = {0x8d, 0x10, 0xae};
    return writeBytes(0x00, commands, sizeof(commands), error);
  }

  void close() {
    if (descriptor_ >= 0)
      ::close(descriptor_);
    descriptor_ = -1;
    rtos_ = false;
  }

private:
  bool writeRtosFrame(const Canvas &canvas, std::string &error) {
    const auto &frame = canvas.data();
    const ssize_t written =
        ::write(descriptor_, frame.data(), frame.size());
    if (written != static_cast<ssize_t>(frame.size())) {
      error = "write OLED: " +
              std::string(written < 0 ? std::strerror(errno) : "short write");
      return false;
    }
    return true;
  }

  bool writeBytes(uint8_t control, const uint8_t *bytes, std::size_t count,
                  std::string &error) {
    std::array<uint8_t, kMaxWidth + 1> message{};
    if (count > kMaxWidth) {
      error = "OLED write payload is too large";
      return false;
    }
    message[0] = control;
    std::copy(bytes, bytes + count, message.begin() + 1);
    const ssize_t written = ::write(descriptor_, message.data(), count + 1);
    if (written != static_cast<ssize_t>(count + 1)) {
      error = "write OLED: " +
              std::string(written < 0 ? std::strerror(errno) : "short write");
      return false;
    }
    return true;
  }

  DisplayProfile profile_;
  int descriptor_ = -1;
  bool rtos_ = false;
};

bool openDisplay(Device &device, const std::string &path, int address,
                 std::chrono::milliseconds timeout, std::string &error) {
  const auto deadline = Clock::now() + timeout;
  do {
    if (device.open(path, address, error))
      return true;
    std::this_thread::sleep_for(100ms);
  } while (Clock::now() < deadline);
  return false;
}

} // namespace onekvm::oled
