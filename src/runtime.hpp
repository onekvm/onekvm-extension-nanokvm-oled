#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace onekvm::oled {

uint64_t waitTimeoutUsec(Clock::time_point now, Clock::time_point deadline);
bool decodeStatus(std::string_view payload, Status &status,
                  std::string &error);
bool controllerPlan(std::string_view input, std::string &output,
                    std::string &error);
int applicationMain(int argc, char **argv);

} // namespace onekvm::oled
