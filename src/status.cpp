#include "runtime.hpp"

#include "json.hpp"

#include <algorithm>

namespace onekvm::oled {

void IpRotation::update(const Status &status, Clock::time_point now) {
  auto addresses = status.ipv4Addresses;
  if (addresses.empty() && !status.ip.empty())
    addresses.push_back(status.ip);
  if (addresses == addresses_)
    return;
  addresses_ = std::move(addresses);
  index_ = 0;
  deadline_ = addresses_.size() > 1 ? now + 10s : Clock::time_point::max();
}

void IpRotation::advance(Clock::time_point now) {
  if (addresses_.size() < 2 || now < deadline_)
    return;
  index_ = (index_ + 1) % addresses_.size();
  deadline_ = now + 10s;
}

std::string IpRotation::current() const {
  return addresses_.empty() ? std::string{} : addresses_[index_];
}

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
  status.ip.clear();
  status.ipv4Addresses.clear();
  if (const Json *network = root.get("network");
      network != nullptr && network->type == Json::Type::Object) {
    if (!jsonString(*network, "hostname", {}, status.hostname, error) ||
        !jsonString(*network, "ip", {}, status.ip, error))
      return false;
    if (!status.ip.empty())
      status.ipv4Addresses.push_back(status.ip);
    if (const Json *addresses = network->get("ipv4_addresses")) {
      if (addresses->type != Json::Type::Array) {
        error = "ipv4_addresses must be an array";
        return false;
      }
      for (const auto &address : addresses->array) {
        if (address.type != Json::Type::String) {
          error = "ipv4_addresses entries must be strings";
          return false;
        }
        if (!address.string.empty() &&
            std::find(status.ipv4Addresses.begin(), status.ipv4Addresses.end(),
                      address.string) == status.ipv4Addresses.end())
          status.ipv4Addresses.push_back(address.string);
      }
    }
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
  jsonString(root, "machine", {}, status.machine, error);
  jsonString(root, "version", {}, status.version, error);
  error.clear();
  if (const Json *system = root.get("system");
      system != nullptr && system->type == Json::Type::Object) {
    jsonString(*system, "processor", {}, status.processor, error);
    jsonString(*system, "kernel_version", {}, status.kernel, error);
    error.clear();
    if (const Json *cpu = system->get("cpu_percent");
        cpu != nullptr && cpu->type == Json::Type::Number)
      status.cpuPercent = static_cast<int>(cpu->number + (cpu->number < 0 ? -0.5 : 0.5));
    if (const Json *used = system->get("memory_used_bytes");
        used != nullptr && used->type == Json::Type::Number && used->number >= 0)
      status.memoryUsed = static_cast<uint64_t>(used->number);
    if (const Json *total = system->get("memory_total_bytes");
        total != nullptr && total->type == Json::Type::Number && total->number >= 0)
      status.memoryTotal = static_cast<uint64_t>(total->number);
    if (const Json *disk = system->get("storage_total_bytes");
        disk != nullptr && disk->type == Json::Type::Number && disk->number >= 0)
      status.storageTotal = static_cast<uint64_t>(disk->number);
    if (const Json *free = system->get("storage_available_bytes");
        free != nullptr && free->type == Json::Type::Number && free->number >= 0)
      status.storageAvailable = static_cast<uint64_t>(free->number);
  }
  return true;
}

} // namespace onekvm::oled
