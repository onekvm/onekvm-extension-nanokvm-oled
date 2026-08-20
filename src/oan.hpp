#pragma once

#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <string_view>
#include <vector>

namespace onekvm::oled {

inline constexpr std::string_view kOan1Magic = "OAN1";
inline constexpr std::string_view kOan2Magic = "OAN2";
inline constexpr unsigned char kOanOpFrame = 1;
inline constexpr unsigned char kOanOpSleep = 2;
inline constexpr unsigned char kOanOpInvertX = 3;
inline constexpr unsigned char kOanOpInvertY = 4;
inline constexpr unsigned char kOanOpLoop = 5;
inline constexpr uint32_t kOanMaxEvents = 65536U;
inline constexpr std::size_t kOanMaxBytes = 32U * 1024U * 1024U;
inline constexpr std::size_t kOanHeaderBytes = 14;
inline constexpr uint32_t kOanNoLoop = std::numeric_limits<uint32_t>::max();

struct OanInfo {
  bool version2 = false;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t fps = 0;
  uint32_t stored_frames = 0;
  uint32_t events = 0;
  uint32_t duration_ms = 0;
  std::size_t frame_bytes = 0;
  std::uintmax_t first_frame_offset = 0;
};

struct OanEvent {
  unsigned char op = 0;
  uint16_t delay_ms = 0;
  uint16_t arg = 0;
  int32_t frame_index = -1;
};

struct OanStep {
  uint16_t delay_ms = 0;
  int32_t frame_index = -1;
};

inline uint16_t oanU16(const unsigned char *bytes) {
  return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

inline uint32_t oanU32(const unsigned char *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

inline bool oanDimensionsOk(uint16_t width, uint16_t height) {
  return width >= 8 && width <= 256 && height >= 8 && height <= 256 &&
         (height % 8U) == 0;
}

inline bool addDuration(uint32_t &duration_ms, uint16_t delay_ms) {
  const uint32_t next = duration_ms + delay_ms;
  if (next < duration_ms)
    return false;
  duration_ms = next;
  return true;
}

inline bool readU16(std::istream &in, uint16_t &value, std::uintmax_t &offset) {
  unsigned char bytes[2];
  in.read(reinterpret_cast<char *>(bytes), 2);
  if (!in)
    return false;
  offset += 2;
  value = oanU16(bytes);
  return true;
}

inline void oanInvertX(unsigned char *buf, uint16_t width, uint16_t height,
                       uint16_t x) {
  if (x >= width || height < 8)
    return;
  const unsigned pages = static_cast<unsigned>(height / 8);
  for (unsigned page = 0; page < pages; ++page)
    buf[static_cast<std::size_t>(page) * width + x] ^= 0xff;
}

inline void oanInvertY(unsigned char *buf, uint16_t width, uint16_t height,
                       uint16_t y) {
  if (y >= height)
    return;
  const unsigned char bit =
      static_cast<unsigned char>(1U << (y & 7U));
  const unsigned page = static_cast<unsigned>(y >> 3);
  for (uint16_t x = 0; x < width; ++x)
    buf[static_cast<std::size_t>(page) * width + x] ^= bit;
}

inline bool inspectOan(std::istream &in, std::uintmax_t size, OanInfo &info) {
  info = {};
  if (size < kOanHeaderBytes || size > kOanMaxBytes)
    return false;
  unsigned char header[kOanHeaderBytes];
  in.read(reinterpret_cast<char *>(header),
          static_cast<std::streamsize>(kOanHeaderBytes));
  if (!in)
    return false;
  const std::string_view magic(reinterpret_cast<char *>(header), 4);
  info.version2 = magic == kOan2Magic;
  if (!info.version2 && magic != kOan1Magic)
    return false;
  info.width = oanU16(header + 4);
  info.height = oanU16(header + 6);
  info.fps = oanU16(header + 8);
  info.events = oanU32(header + 10);
  if (!oanDimensionsOk(info.width, info.height) || info.events == 0 ||
      info.events > kOanMaxEvents)
    return false;
  info.frame_bytes = static_cast<std::size_t>(info.width) *
                     static_cast<std::size_t>(info.height) / 8U;
  if (info.frame_bytes == 0)
    return false;
  if (!info.version2) {
    if (info.fps == 0 || info.fps > 120)
      return false;
    const std::uintmax_t payload =
        static_cast<std::uintmax_t>(info.events) * info.frame_bytes;
    if (payload / info.frame_bytes != info.events)
      return false;
    if (size != kOanHeaderBytes + payload)
      return false;
    info.stored_frames = info.events;
    info.first_frame_offset = kOanHeaderBytes;
    const uint32_t period = 1000U / info.fps;
    const uint16_t delay = static_cast<uint16_t>(period ? period : 1U);
    for (uint32_t i = 0; i < info.events; ++i) {
      if (!addDuration(info.duration_ms, delay))
        return false;
    }
    return true;
  }
  if (info.fps > 120)
    return false;
  std::uintmax_t offset = kOanHeaderBytes;
  for (uint32_t i = 0; i < info.events; ++i) {
    unsigned char op = 0;
    in.read(reinterpret_cast<char *>(&op), 1);
    if (!in)
      return false;
    ++offset;
    if (op == kOanOpFrame) {
      uint16_t delay_ms = 0;
      if (!readU16(in, delay_ms, offset))
        return false;
      if (!addDuration(info.duration_ms, delay_ms))
        return false;
      if (info.stored_frames == 0)
        info.first_frame_offset = offset;
      if (offset + info.frame_bytes > size)
        return false;
      in.seekg(static_cast<std::streamoff>(info.frame_bytes), std::ios::cur);
      if (!in)
        return false;
      offset += info.frame_bytes;
      ++info.stored_frames;
      continue;
    }
    if (op == kOanOpSleep) {
      uint16_t delay_ms = 0;
      if (!readU16(in, delay_ms, offset))
        return false;
      if (!addDuration(info.duration_ms, delay_ms))
        return false;
      continue;
    }
    if (op == kOanOpInvertX || op == kOanOpInvertY) {
      uint16_t coord = 0;
      uint16_t delay_ms = 0;
      if (!readU16(in, coord, offset) || !readU16(in, delay_ms, offset))
        return false;
      if (op == kOanOpInvertX && coord >= info.width)
        return false;
      if (op == kOanOpInvertY && coord >= info.height)
        return false;
      if (!addDuration(info.duration_ms, delay_ms))
        return false;
      continue;
    }
    if (op == kOanOpLoop) {
      uint16_t target = 0;
      if (!readU16(in, target, offset))
        return false;
      if (target >= info.events || target == i)
        return false;
      continue;
    }
    return false;
  }
  return info.stored_frames > 0 && offset == size;
}

inline bool loadOanEvents(std::istream &in, const OanInfo &info,
                          std::vector<unsigned char> &pixels,
                          std::vector<OanEvent> &events) {
  pixels.clear();
  events.clear();
  pixels.reserve(info.stored_frames * info.frame_bytes);
  events.reserve(info.events);
  if (!info.version2) {
    pixels.resize(info.stored_frames * info.frame_bytes);
    in.read(reinterpret_cast<char *>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));
    if (!in)
      return false;
    const uint16_t period =
        static_cast<uint16_t>(info.fps ? 1000U / info.fps : 1U);
    const uint16_t delay = period ? period : 1;
    for (uint32_t i = 0; i < info.stored_frames; ++i)
      events.push_back(OanEvent{kOanOpFrame, delay, 0, static_cast<int32_t>(i)});
    return true;
  }
  int32_t last_frame = -1;
  for (uint32_t i = 0; i < info.events; ++i) {
    unsigned char op = 0;
    in.read(reinterpret_cast<char *>(&op), 1);
    if (!in)
      return false;
    OanEvent event{};
    event.op = op;
    if (op == kOanOpFrame) {
      unsigned char delay[2];
      in.read(reinterpret_cast<char *>(delay), 2);
      const auto stored = pixels.size();
      pixels.resize(stored + info.frame_bytes);
      in.read(reinterpret_cast<char *>(pixels.data() + stored),
              static_cast<std::streamsize>(info.frame_bytes));
      if (!in)
        return false;
      last_frame = static_cast<int32_t>(stored / info.frame_bytes);
      event.delay_ms = oanU16(delay);
      event.frame_index = last_frame;
      events.push_back(event);
      continue;
    }
    if (op == kOanOpSleep) {
      unsigned char delay[2];
      in.read(reinterpret_cast<char *>(delay), 2);
      if (!in || last_frame < 0)
        return false;
      event.delay_ms = oanU16(delay);
      event.frame_index = last_frame;
      events.push_back(event);
      continue;
    }
    if (op == kOanOpInvertX || op == kOanOpInvertY) {
      unsigned char coord[2];
      unsigned char delay[2];
      in.read(reinterpret_cast<char *>(coord), 2);
      in.read(reinterpret_cast<char *>(delay), 2);
      if (!in || last_frame < 0)
        return false;
      event.arg = oanU16(coord);
      event.delay_ms = oanU16(delay);
      event.frame_index = last_frame;
      events.push_back(event);
      continue;
    }
    if (op == kOanOpLoop) {
      unsigned char target[2];
      in.read(reinterpret_cast<char *>(target), 2);
      if (!in)
        return false;
      event.arg = oanU16(target);
      events.push_back(event);
      continue;
    }
    return false;
  }
  return !events.empty();
}

inline bool expandOanRaster(const OanInfo &info,
                            const std::vector<unsigned char> &source_pixels,
                            const std::vector<OanEvent> &events,
                            std::vector<unsigned char> &raster,
                            std::vector<OanStep> &steps, uint32_t &loop_index) {
  raster.clear();
  steps.clear();
  loop_index = kOanNoLoop;
  if (info.frame_bytes == 0)
    return false;
  std::vector<unsigned char> buf(info.frame_bytes, 0);
  bool have = false;
  int32_t last_index = -1;
  auto push = [&](uint16_t delay_ms) {
    if (!have)
      return;
    if (last_index < 0 ||
        std::memcmp(raster.data() +
                        static_cast<std::size_t>(last_index) * info.frame_bytes,
                    buf.data(), info.frame_bytes) != 0) {
      last_index = static_cast<int32_t>(raster.size() / info.frame_bytes);
      raster.insert(raster.end(), buf.begin(), buf.end());
    }
    steps.push_back(OanStep{delay_ms, last_index});
  };
  uint32_t pc = 0;
  bool taken_back = false;
  uint32_t loop_event = kOanNoLoop;
  unsigned guard = 0;
  while (pc < events.size() && guard++ < kOanMaxEvents * 2U) {
    const OanEvent &event = events[pc];
    if (event.op == kOanOpLoop) {
      if (event.arg >= events.size() || event.arg == pc)
        return false;
      if (event.arg > pc) {
        pc = event.arg;
        continue;
      }
      if (!taken_back) {
        taken_back = true;
        loop_event = pc;
        loop_index = static_cast<uint32_t>(steps.size());
        pc = event.arg;
        continue;
      }
      if (pc == loop_event)
        break;
      pc = event.arg;
      continue;
    }
    if (event.op == kOanOpFrame) {
      if (event.frame_index < 0)
        return false;
      const std::size_t offset =
          static_cast<std::size_t>(event.frame_index) * info.frame_bytes;
      if (offset + info.frame_bytes > source_pixels.size())
        return false;
      std::memcpy(buf.data(), source_pixels.data() + offset, info.frame_bytes);
      have = true;
      push(event.delay_ms);
    } else if (event.op == kOanOpSleep) {
      push(event.delay_ms);
    } else if (event.op == kOanOpInvertX) {
      if (!have)
        return false;
      oanInvertX(buf.data(), info.width, info.height, event.arg);
      push(event.delay_ms);
    } else if (event.op == kOanOpInvertY) {
      if (!have)
        return false;
      oanInvertY(buf.data(), info.width, info.height, event.arg);
      push(event.delay_ms);
    } else {
      return false;
    }
    ++pc;
  }
  if (steps.empty())
    return false;
  if (loop_index >= steps.size())
    loop_index = kOanNoLoop;
  return true;
}

inline bool loadOan(std::istream &in, std::uintmax_t size, OanInfo &info,
                    std::vector<unsigned char> &pixels,
                    std::vector<OanEvent> &events) {
  const auto start = in.tellg();
  if (!inspectOan(in, size, info))
    return false;
  in.clear();
  if (start == std::streampos(-1) || !in.seekg(start))
    return false;
  in.seekg(static_cast<std::streamoff>(kOanHeaderBytes), std::ios::cur);
  return loadOanEvents(in, info, pixels, events);
}

inline uint32_t oanRasterDuration(const std::vector<OanStep> &steps) {
  uint32_t duration_ms = 0;
  for (const OanStep &step : steps)
    (void)addDuration(duration_ms, step.delay_ms);
  return duration_ms;
}

inline int oanFrameAt(const std::vector<OanStep> &steps, uint32_t duration_ms,
                      uint32_t loop_index, uint64_t elapsed_ms) {
  if (steps.empty() || duration_ms == 0)
    return -1;
  uint64_t cursor = elapsed_ms;
  uint32_t start = 0;
  if (loop_index != kOanNoLoop && loop_index < steps.size()) {
    uint32_t prefix_ms = 0;
    for (uint32_t i = 0; i < loop_index; ++i)
      prefix_ms += steps[i].delay_ms;
    uint32_t cycle_ms = 0;
    for (uint32_t i = loop_index; i < steps.size(); ++i)
      cycle_ms += steps[i].delay_ms;
    if (elapsed_ms < prefix_ms) {
      cursor = elapsed_ms;
      start = 0;
    } else if (cycle_ms == 0) {
      return steps.back().frame_index;
    } else {
      cursor = (elapsed_ms - prefix_ms) % cycle_ms;
      start = loop_index;
    }
  } else if (elapsed_ms >= duration_ms) {
    return steps.back().frame_index;
  }
  uint32_t remaining = static_cast<uint32_t>(cursor);
  for (uint32_t i = start; i < steps.size(); ++i) {
    if (remaining < steps[i].delay_ms)
      return steps[i].frame_index;
    remaining -= steps[i].delay_ms;
  }
  return steps.back().frame_index;
}

} // namespace onekvm::oled
