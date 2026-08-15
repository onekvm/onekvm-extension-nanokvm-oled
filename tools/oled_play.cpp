#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
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
  std::ifstream input(argv[1], std::ios::binary);
  char magic[4]; uint16_t width, height, fps; uint32_t frames;
  if (!input.read(magic, 4) || std::memcmp(magic, "OAN1", 4) != 0 ||
      !input.read(reinterpret_cast<char *>(&width), 2) ||
      !input.read(reinterpret_cast<char *>(&height), 2) ||
      !input.read(reinterpret_cast<char *>(&fps), 2) ||
      !input.read(reinterpret_cast<char *>(&frames), 4) || !fps ||
      width > 128 || !height || height % 8 || !frames) return 3;
  const std::size_t bytes = static_cast<std::size_t>(width) * height / 8;
  std::vector<uint8_t> frame(bytes);
  int fd = ::open(argv[2], O_RDWR | O_CLOEXEC);
  if (fd < 0 || ioctl(fd, I2C_SLAVE,
                     argc >= 4 ? std::strtol(argv[3], nullptr, 0) : 0x3d) < 0)
    return 4;
  const uint8_t init[] = {0xae,0x00,0x10,0x40,0x81,0xcf,0xa1,0xc8,0xa6,0xa8,
    static_cast<uint8_t>(height-1),0xd3,0x00,0xd5,0x80,0xd9,0xf1,0xda,0x12,
    0xdb,0x40,0x20,0x02,0x8d,0x14,0xa4,0xa6,0xaf,0xa0,0xc0};
  if (!write_block(fd, 0, init, sizeof(init))) return 5;
  unsigned loops = argc >= 5 ? std::strtoul(argv[4], nullptr, 0) : 1;
  const auto interval = std::chrono::microseconds(1000000 / fps);
  for (unsigned loop = 0; loop < loops; ++loop) {
    for (uint32_t n = 0; n < frames; ++n) {
      if (!input.read(reinterpret_cast<char *>(frame.data()), bytes)) return 6;
      auto start = std::chrono::steady_clock::now();
      for (uint16_t page = 0; page < height / 8; ++page) {
        const uint8_t pos[] = {static_cast<uint8_t>(0xb0 + page), 0x10, 0x00};
        if (!write_block(fd, 0, pos, sizeof(pos)) ||
            !write_block(fd, 0x40, frame.data() + page * width, width)) return 7;
      }
      std::this_thread::sleep_until(start + interval);
    }
  }
  const uint8_t off[] = {0x8d,0x10,0xae};
  write_block(fd, 0, off, sizeof(off));
  ::close(fd);
  return 0;
}
