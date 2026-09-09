#pragma once

#include "json.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <string>
#include <string_view>
#include <utility>

namespace onekvm::oled {

class StateSource {
public:
  ~StateSource() {
    sd_bus_slot_unref(statusSlot_);
    sd_bus_slot_unref(ownerSlot_);
    sd_bus_slot_unref(alertSlot_);
    sd_bus_slot_unref(dismissSlot_);
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
    const std::string alertMatch =
        "type='signal',path='" + std::string(kScreenPath) + "',interface='" +
        std::string(kScreenInterface) + "',member='Alert'";
    result = sd_bus_add_match(bus_, &alertSlot_, alertMatch.c_str(),
                              &StateSource::onAlert, this);
    if (result < 0)
      return busError("subscribe screen alerts", result, error);
    const std::string dismissMatch =
        "type='signal',path='" + std::string(kScreenPath) + "',interface='" +
        std::string(kScreenInterface) + "',member='Dismissed'";
    result = sd_bus_add_match(bus_, &dismissSlot_, dismissMatch.c_str(),
                              &StateSource::onDismissed, this);
    if (result < 0)
      return busError("subscribe screen dismissals", result, error);
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
    for (;;) {
      const uint64_t remaining = waitTimeoutUsec(Clock::now(), deadline);
      if (remaining == 0)
        return true;
      const int fd = sd_bus_get_fd(bus_);
      if (fd < 0)
        return busError("bus fd", fd, error);
      const int events = sd_bus_get_events(bus_);
      if (events < 0)
        return busError("bus events", events, error);
      pollfd pfd{};
      pfd.fd = fd;
      pfd.events = static_cast<short>(events);
      const int timeoutMs = static_cast<int>(
          std::min<uint64_t>((remaining + 999U) / 1000U, 1000U));
      const int result = ::poll(&pfd, 1, timeoutMs);
      if (result < 0) {
        if (errno == EINTR)
          continue;
        error = "wait for system bus: " + std::string(std::strerror(errno));
        return false;
      }
      return true;
    }
  }

  bool available() const { return available_; }
  bool take(Status &output) {
    if (!updated_)
      return false;
    updated_ = false;
    output = status_;
    return true;
  }

  bool takeAlert(OledAlert &output) {
    if (!alertUpdated_)
      return false;
    alertUpdated_ = false;
    output = alert_;
    return true;
  }

  bool takeDismiss(std::string &id) {
    if (!dismissUpdated_)
      return false;
    dismissUpdated_ = false;
    id = dismissedId_;
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

  static int onAlert(sd_bus_message *message, void *userdata, sd_bus_error *) {
    auto *source = static_cast<StateSource *>(userdata);
    const char *id = nullptr;
    const char *payload = nullptr;
    if (sd_bus_message_read(message, "ss", &id, &payload) < 0 ||
        id == nullptr || payload == nullptr)
      return 0;
    Json document;
    std::string error;
    if (!parseJSON(payload, document, error) ||
        document.type != Json::Type::Object)
      return 0;
    OledAlert alert;
    alert.id = id;
    if (!jsonString(document, "title", {}, alert.title, error))
      alert.title.clear();
    if (!jsonString(document, "body", {}, alert.body, error))
      alert.body.clear();
    int ttl = 0;
    if (!jsonInt(document, "ttl_ms", 0, ttl, error) || ttl < 0)
      ttl = 0;
    alert.ttl_ms = static_cast<uint32_t>(ttl);
    if (alert.title.empty() && alert.body.empty())
      return 0;
    source->alert_ = std::move(alert);
    source->alertUpdated_ = true;
    return 0;
  }

  static int onDismissed(sd_bus_message *message, void *userdata,
                         sd_bus_error *) {
    auto *source = static_cast<StateSource *>(userdata);
    const char *id = nullptr;
    if (sd_bus_message_read(message, "s", &id) < 0 || id == nullptr)
      return 0;
    source->dismissedId_ = id;
    source->dismissUpdated_ = true;
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
  sd_bus_slot *alertSlot_ = nullptr;
  sd_bus_slot *dismissSlot_ = nullptr;
  Status status_;
  OledAlert alert_;
  std::string dismissedId_;
  bool available_ = false;
  bool updated_ = false;
  bool alertUpdated_ = false;
  bool dismissUpdated_ = false;
  Clock::time_point retryAt_{};
};

} // namespace onekvm::oled
