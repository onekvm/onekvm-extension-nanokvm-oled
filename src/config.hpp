#pragma once

#include "types.hpp"

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace onekvm::oled {

bool displayProfileForVariant(std::string_view variant,
                              std::string_view hardwareRevision,
                              DisplayProfile &output, std::string &error);
Layout layoutFromLines(std::array<std::string, 4> lines, int rotation,
                       int displayWidth = kPcieWidth,
                       int displayHeight = kPcieHeight);
Layout layoutFromAlert(std::string_view title, std::string_view body,
                       int rotation, int displayWidth = kPcieWidth,
                       int displayHeight = kPcieHeight);
bool isBootConfirmAlert(std::string_view kind, std::string_view title,
                        std::string_view body);
Layout layoutFromBootConfirm(int rotation, int displayWidth = kPcieWidth,
                             int displayHeight = kPcieHeight);
Layout layoutForScreenAlert(const OledAlert &alert, int rotation,
                            int displayWidth = kPcieWidth,
                            int displayHeight = kPcieHeight);
Layout pcieDefaultLayout();
Layout cubeDefaultLayout();
bool parseLayout(std::string_view raw,
                 const std::array<std::string, 4> &legacy, int rotation,
                 Layout &output, std::string &error,
                 int displayWidth = kPcieWidth,
                 int displayHeight = kPcieHeight,
                 bool useDeviceDefault = false);
bool readFile(const std::string &path, std::string &output, std::string &error,
              std::size_t limit = 1U << 20);
bool loadDisplayProfile(const std::string &profilePath,
                        const std::string &hardwarePath,
                        DisplayProfile &output, std::string &error);
bool loadConfig(const std::string &path, Config &output, std::string &raw,
                std::string &error, int displayWidth = kPcieWidth,
                int displayHeight = kPcieHeight);
bool updateCustomVariable(std::map<std::string, std::string> &variables,
                          std::string_view key, std::string_view value,
                          std::string &error);
bool removeCustomVariable(std::map<std::string, std::string> &variables,
                          std::string_view key, std::string &error);

} // namespace onekvm::oled
