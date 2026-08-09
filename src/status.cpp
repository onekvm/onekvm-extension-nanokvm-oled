#include "runtime.hpp"

#include "json.hpp"

namespace onekvm::oled {

bool decodeStatus(std::string_view payload, Status &status,
                  std::string &error) {
  Json root;
  if (!parseJSON(payload, root, error) || root.type != Json::Type::Object)
    return false;
  int uptime = 0;
  if (!jsonInt(root, "uptime_seconds", 0, uptime, error))
    return false;
  status.uptime = uptime;
  if (const Json *video = root.get("video");
      video != nullptr && video->type == Json::Type::Object) {
    if (!jsonBool(*video, "active", false, status.active, error) ||
        !jsonString(*video, "codec", {}, status.codec, error) ||
        !jsonInt(*video, "fps", 0, status.fps, error) ||
        !jsonInt(*video, "actual_fps", status.fps, status.actualFPS, error) ||
        !jsonInt(*video, "input_width", 0, status.inputWidth, error) ||
        !jsonInt(*video, "input_height", 0, status.inputHeight, error) ||
        !jsonInt(*video, "resolution", 0, status.resolution, error))
      return false;
    if (!jsonBool(*video, "hdmi_connected", status.active,
                  status.hdmiConnected, error))
      return false;
  }
  if (const Json *network = root.get("network");
      network != nullptr && network->type == Json::Type::Object) {
    if (!jsonString(*network, "hostname", {}, status.hostname, error) ||
        !jsonString(*network, "ip", {}, status.ip, error))
      return false;
    if (!jsonBool(*network, "ethernet_connected", !status.ip.empty(),
                  status.ethernetConnected, error))
      return false;
    if (!jsonBool(*network, "wifi_available", false, status.wifiAvailable,
                  error) ||
        !jsonBool(*network, "wifi_connected", false, status.wifiConnected,
                  error))
      return false;
  }
  if (const Json *hid = root.get("hid");
      hid != nullptr && hid->type == Json::Type::Object) {
    if (!jsonBool(*hid, "available", false, status.hidAvailable, error) ||
        !jsonBool(*hid, "connected", false, status.hidConnected, error))
      return false;
  }
  return true;
}

} // namespace onekvm::oled
