#include "runtime.hpp"

#include "json.hpp"
#include "types.hpp"

#include <string>
#include <string_view>

namespace onekvm::oled {

bool controllerPlan(std::string_view input, std::string &output,
                    std::string &error) {
  if (input.size() > (1U << 20)) {
    error = "controller event exceeds 1 MiB";
    return false;
  }
  Json event;
  if (!parseJSON(input, event, error) || event.type != Json::Type::Object)
    return false;
  int apiVersion = 0;
  std::string eventName;
  std::string extensionID;
  std::string method;
  bool pluginEnabled = false;
  if (!jsonInt(event, "api_version", 0, apiVersion, error) ||
      !jsonString(event, "event", {}, eventName, error) ||
      !jsonString(event, "extension_id", {}, extensionID, error) ||
      !jsonBool(event, "enabled", false, pluginEnabled, error) ||
      !jsonString(event, "method", {}, method, error))
    return false;
  if (apiVersion != 1 || extensionID != kExtensionID) {
    error = "unsupported controller event contract";
    return false;
  }
  bool displayEnabled = false;
  if (const Json *settings = event.get("settings");
      settings != nullptr && settings->type == Json::Type::Object) {
    if (!jsonBool(*settings, "display_enabled", false, displayEnabled, error))
      return false;
  }
  if (eventName == "invoke") {
    if (method != "off") {
      error = "unknown OLED public method " + method;
      return false;
    }
    output =
        R"({"actions":[{"type":"remove","name":"display"},{"type":"exec","name":"off","command":["bin/onekvm-nanokvm-oled","--off"],"timeout_seconds":8}],"response":{"display":"off"}})";
    return true;
  }
  if (eventName != "config_changed" && eventName != "enable" &&
      eventName != "disable" && eventName != "reconcile") {
    error = "unsupported OLED lifecycle event " + eventName;
    return false;
  }
  if (eventName != "disable" && pluginEnabled && displayEnabled) {
    if (eventName == "config_changed") {
      output =
          R"({"actions":[{"type":"remove","name":"display"},{"type":"ensure","name":"display","command":["bin/onekvm-nanokvm-oled"],"restart":"on-failure"}]})";
    } else {
      output =
          R"({"actions":[{"type":"ensure","name":"display","command":["bin/onekvm-nanokvm-oled"],"restart":"on-failure"}]})";
    }
  } else {
    output =
        R"({"actions":[{"type":"remove","name":"display"},{"type":"exec","name":"off","command":["bin/onekvm-nanokvm-oled","--off"],"timeout_seconds":8}]})";
  }
  return true;
}

} // namespace onekvm::oled
