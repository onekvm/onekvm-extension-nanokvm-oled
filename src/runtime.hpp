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
bool controllerPlan(std::string_view input, std::string &output,
                    std::string &error);
int applicationMain(int argc, char **argv);

} // namespace onekvm::oled
