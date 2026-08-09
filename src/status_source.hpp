#pragma once

#include "runtime.hpp"
#include "types.hpp"

#include <systemd/sd-bus.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace onekvm::oled {

class StateSource {
public:
  ~StateSource() {
    sd_bus_slot_unref(statusSlot_);
    sd_bus_slot_unref(ownerSlot_);
    sd_bus_unref(bus_);
  }

  bool connect(std::string &error) {
    int result = sd_bus_default_system(&bus_);
    if (result < 0)
      return busError("connect system bus", result, error);
    const std::string statusMatch =
        "type='signal',path='" + std::string(kStatusPath) + "',interface='" +
        std::string(kStatusInterface) + "',member='StateChanged'";
    result = sd_bus_add_match(bus_, &statusSlot_, statusMatch.c_str(),
                              &StateSource::onStatus, this);
    if (result < 0)
      return busError("subscribe Core status", result, error);
    const std::string ownerMatch =
        "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/"
        "DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0="
        "'" +
        std::string(kCoreBusName) + "'";
    result = sd_bus_add_match(bus_, &ownerSlot_, ownerMatch.c_str(),
                              &StateSource::onOwner, this);
    if (result < 0)
      return busError("subscribe Core ownership", result, error);
    fetch(error);
    return true;
  }

  bool process(std::string &error) {
    while (bus_ != nullptr) {
      const int result = sd_bus_process(bus_, nullptr);
      if (result < 0)
        return busError("process system bus", result, error);
      if (result == 0)
        break;
    }
    if (!available_ && Clock::now() >= retryAt_) {
      std::string ignored;
      fetch(ignored);
      retryAt_ = Clock::now() + 1s;
    }
    return true;
  }

  bool waitUntil(Clock::time_point deadline, std::string &error) {
    if (bus_ == nullptr) {
      error = "wait for system bus: not connected";
      return false;
    }
    const int result =
        sd_bus_wait(bus_, waitTimeoutUsec(Clock::now(), deadline));
    if (result < 0 && result != -EINTR)
      return busError("wait for system bus", result, error);
    return true;
  }

  bool available() const { return available_; }
  bool take(Status &output) {
    if (!updated_)
      return false;
    updated_ = false;
    output = status_;
    return true;
  }

private:
  static int onStatus(sd_bus_message *message, void *userdata, sd_bus_error *) {
    auto *source = static_cast<StateSource *>(userdata);
    const char *payload = nullptr;
    if (sd_bus_message_read(message, "s", &payload) < 0 || payload == nullptr)
      return 0;
    Status next;
    std::string error;
    if (decodeStatus(payload, next, error)) {
      source->status_ = std::move(next);
      source->available_ = true;
      source->updated_ = true;
    }
    return 0;
  }

  static int onOwner(sd_bus_message *message, void *userdata, sd_bus_error *) {
    auto *source = static_cast<StateSource *>(userdata);
    const char *name = nullptr;
    const char *oldOwner = nullptr;
    const char *newOwner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &oldOwner, &newOwner) < 0)
      return 0;
    (void)name;
    (void)oldOwner;
    source->available_ = newOwner != nullptr && *newOwner != '\0';
    if (source->available_) {
      std::string ignored;
      source->fetch(ignored);
    }
    return 0;
  }

  bool fetch(std::string &error) {
    sd_bus_error busErrorValue = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    const int result = sd_bus_call_method(
        bus_, kCoreBusName.data(), kStatusPath.data(), kStatusInterface.data(),
        "GetState", &busErrorValue, &reply, "");
    if (result < 0) {
      error = "get Core D-Bus status: " +
              std::string(busErrorValue.message != nullptr
                              ? busErrorValue.message
                              : std::strerror(-result));
      sd_bus_error_free(&busErrorValue);
      sd_bus_message_unref(reply);
      available_ = false;
      return false;
    }
    const char *payload = nullptr;
    const int readResult = sd_bus_message_read(reply, "s", &payload);
    if (readResult < 0 || payload == nullptr ||
        !decodeStatus(payload, status_, error)) {
      if (error.empty())
        error = "invalid Core D-Bus status reply";
      sd_bus_error_free(&busErrorValue);
      sd_bus_message_unref(reply);
      available_ = false;
      return false;
    }
    sd_bus_error_free(&busErrorValue);
    sd_bus_message_unref(reply);
    available_ = true;
    updated_ = true;
    return true;
  }

  static bool busError(std::string_view operation, int result,
                       std::string &error) {
    error = std::string(operation) + ": " + std::strerror(-result);
    return false;
  }

  sd_bus *bus_ = nullptr;
  sd_bus_slot *statusSlot_ = nullptr;
  sd_bus_slot *ownerSlot_ = nullptr;
  Status status_;
  bool available_ = false;
  bool updated_ = false;
  Clock::time_point retryAt_{};
};

} // namespace onekvm::oled
