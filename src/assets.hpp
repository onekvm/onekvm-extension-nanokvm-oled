#pragma once

#include <string>
#include <string_view>

namespace onekvm::oled {

bool runAssetServer(const std::string &socketPath, const std::string &dataPath,
                    std::string &error);
bool configReferencesAsset(const std::string &configPath,
                           std::string_view assetName, bool &referenced,
                           std::string &error);

} // namespace onekvm::oled
