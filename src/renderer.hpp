#pragma once

#include "canvas.hpp"
#include "types.hpp"

#include <optional>
#include <string>
#include <utility>

namespace onekvm::oled {

std::optional<std::pair<int, int>>
changedColumns(const Canvas &previous, const Canvas &next, int page, int width);
std::string expandTemplate(std::string value, const Status &status);
bool hasLiveLayer(const Config &config);
Canvas render(const Config &config, const Status &status,
              bool liveIndicatorOn = true, int displayWidth = kPcieWidth,
              int displayHeight = kPcieHeight);

} // namespace onekvm::oled
