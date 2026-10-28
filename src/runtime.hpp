#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <atomic>

namespace onekvm::oled {

extern std::atomic_bool stopped;

uint64_t waitTimeoutUsec(Clock::time_point now, Clock::time_point deadline);
bool hasAnimationLayers(const Config &config);
bool decodeStatus(std::string_view payload, Status &status,
                  std::string &error);
class IpRotation {
public:
  void update(const Status &status, Clock::time_point now);
  void advance(Clock::time_point now);
  std::string current() const;
  Clock::time_point deadline() const { return deadline_; }

private:
  std::vector<std::string> addresses_;
  std::size_t index_ = 0;
  Clock::time_point deadline_ = Clock::time_point::max();
};
bool controllerPlan(std::string_view input, std::string &output,
                    std::string &error);
int applicationMain(int argc, char **argv);

} // namespace onekvm::oled
