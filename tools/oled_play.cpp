#include "oan.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <vector>
#include <unistd.h>

static bool write_block(int fd, uint8_t control, const uint8_t *data,
                        std::size_t count) {
  std::vector<uint8_t> message(count + 1);
  message[0] = control;
  std::memcpy(message.data() + 1, data, count);
  return ::write(fd, message.data(), message.size()) ==
         static_cast<ssize_t>(message.size());
}

int main(int argc, char **argv) {
  if (argc < 3 || argc > 5) {
    std::cerr << "usage: oled-play FILE I2C_DEVICE [ADDRESS] [LOOPS]\n";
    return 2;
  }
  std::error_code fileError;
  const auto size = std::filesystem::file_size(argv[1], fileError);
  std::ifstream input(argv[1], std::ios::binary);
  onekvm::oled::OanInfo info{};
  std::vector<unsigned char> pixels;
  std::vector<onekvm::oled::OanEvent> events;
  if (fileError || !onekvm::oled::loadOan(input, size, info, pixels, events) ||
      info.width > 128)
    return 3;
  const std::size_t bytes = info.frame_bytes;
  std::vector<unsigned char> buf(bytes, 0);
  bool have = false;
  int fd = ::open(argv[2], O_RDWR | O_CLOEXEC);
  if (fd < 0 || ioctl(fd, I2C_SLAVE,
                     argc >= 4 ? std::strtol(argv[3], nullptr, 0) : 0x3d) < 0)
    return 4;
  const uint8_t init[] = {0xae,0x00,0x10,0x40,0x81,0xcf,0xa1,0xc8,0xa6,0xa8,
    static_cast<uint8_t>(info.height-1),0xd3,0x00,0xd5,0x80,0xd9,0xf1,0xda,0x12,
    0xdb,0x40,0x20,0x02,0x8d,0x14,0xa4,0xa6,0xaf,0xa0,0xc0};
  if (!write_block(fd, 0, init, sizeof(init))) return 5;
  unsigned loops = argc >= 5 ? std::strtoul(argv[4], nullptr, 0) : 1;
  unsigned cycles = 0;
  uint32_t pc = 0;
  auto draw = [&]() {
    for (uint16_t page = 0; page < info.height / 8; ++page) {
      const uint8_t pos[] = {static_cast<uint8_t>(0xb0 + page), 0x10, 0x00};
      if (!write_block(fd, 0, pos, sizeof(pos)) ||
          !write_block(fd, 0x40, buf.data() + page * info.width, info.width))
        return false;
    }
    return true;
  };
  while (pc < events.size() && cycles < loops) {
    const auto &event = events[pc];
    if (event.op == onekvm::oled::kOanOpLoop) {
      if (event.arg >= events.size())
        break;
      if (event.arg <= pc)
        ++cycles;
      pc = event.arg;
      continue;
    }
    auto start = std::chrono::steady_clock::now();
    if (event.op == onekvm::oled::kOanOpFrame) {
      const std::size_t offset =
          static_cast<std::size_t>(event.frame_index) * bytes;
      std::memcpy(buf.data(), pixels.data() + offset, bytes);
      have = true;
      if (!draw())
        return 7;
    } else if (event.op == onekvm::oled::kOanOpInvertX && have) {
      onekvm::oled::oanInvertX(buf.data(), info.width, info.height, event.arg);
      if (!draw())
        return 7;
    } else if (event.op == onekvm::oled::kOanOpInvertY && have) {
      onekvm::oled::oanInvertY(buf.data(), info.width, info.height, event.arg);
      if (!draw())
        return 7;
    }
    std::this_thread::sleep_until(start +
                                  std::chrono::milliseconds(event.delay_ms));
    ++pc;
  }
  const uint8_t off[] = {0x8d,0x10,0xae};
  write_block(fd, 0, off, sizeof(off));
  ::close(fd);
  return 0;
}
