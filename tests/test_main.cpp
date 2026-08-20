#include "../src/assets.hpp"
#include "../src/config.hpp"
#include "../src/json.hpp"
#include "../src/renderer.hpp"
#include "../src/runtime.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>

using namespace onekvm::oled;

int main() {
  const auto timerBase = Clock::time_point{};
  assert(waitTimeoutUsec(timerBase, timerBase) == 0);
  assert(waitTimeoutUsec(timerBase, timerBase + 1ns) == 1);
  assert(waitTimeoutUsec(timerBase, timerBase + 250ms) == 250000);

  std::string error;
  DisplayProfile pcie;
  assert(displayProfileForVariant("pcie", "p", pcie, error));
  assert(pcie.width == 64 && pcie.height == 32 && pcie.address == 0x3c &&
         pcie.device == "/dev/i2c-5" && pcie.columnOffset == 32 &&
         pcie.pageOffset == 8);
  DisplayProfile cubeAlpha;
  assert(displayProfileForVariant("cube", "alpha", cubeAlpha, error));
  assert(cubeAlpha.width == 128 && cubeAlpha.height == 64 &&
         cubeAlpha.address == 0x3d && cubeAlpha.device == "/dev/i2c-1" &&
         cubeAlpha.columnOffset == 0 && cubeAlpha.pageOffset == 0);
  DisplayProfile cubeBeta;
  assert(displayProfileForVariant("cube", "beta", cubeBeta, error));
  assert(cubeBeta.device == "/dev/i2c-5");
  DisplayProfile lite;
  error.clear();
  assert(!displayProfileForVariant("lite", "", lite, error));
  assert(error == "NanoKVM Lite has no OLED display");

  const std::string machinePath = "/tmp/onekvm-oled-machine-test.json";
  const std::string hardwarePath = "/tmp/onekvm-oled-hardware-test";
  {
    std::ofstream output(machinePath);
    output << R"({"machine":"nanokvm"})";
  }
  {
    std::ofstream output(hardwarePath);
    output << R"({"variant":"cube","revision":"beta","detected":{"oled":true}})";
  }
  DisplayProfile loadedProfile;
  error.clear();
  assert(loadDisplayProfile(machinePath, hardwarePath, loadedProfile, error));
  assert(loadedProfile.variant == "cube" &&
         loadedProfile.device == "/dev/i2c-5");
  ::unlink(machinePath.c_str());
  ::unlink(hardwarePath.c_str());

  std::array<uint16_t, 12> chineseRows{};
  ChineseFont lazyFont;
  lazyFont.configure("web/oled/font-cjk-12.js");
  assert(lazyFont.glyph(0x4e2d, chineseRows) && chineseRows[0] == 0x60);
  assert(chineseFont.load("web/oled/font-cjk-12.js", error));
  assert(chineseFont.glyph(0x4e2d, chineseRows) && chineseRows[0] == 0x60);
  assert(textPixelWidth("A中B", 1) == 22 && textPixelHeight("A中B", 1) == 12);
  assert(textPixelWidth("N+1", 1, 3) == 11 &&
         textPixelWidth("N+1", 1, 4) == 12 &&
         textPixelWidth("N+1", 1, 5) == 13);
  assert(textPixelWidth("123", 1, 3) == 11 &&
         textPixelWidth("123", 1, 4) == 11 &&
         textPixelWidth("123", 1, 5) == 11);
  Canvas latinFont;
  latinFont.text(0, 0, "N+1", 1, 4, 0);
  assert(latinFont.at(0, 0) && latinFont.at(3, 0) &&
         latinFont.at(1, 1) && latinFont.at(6, 1) &&
         latinFont.at(5, 2) && latinFont.at(7, 2) && latinFont.at(10, 0));
  Canvas unshifted = latinFont;
  unshifted.shift(0, 0);
  assert(unshifted == latinFont);
  Layout cubeLegacy = layoutFromLines({"1", "2", "3", "4"}, 0, 128, 64);
  assert(cubeLegacy.elements[0].y == 1 && cubeLegacy.elements[1].y == 17 &&
         cubeLegacy.elements[3].y == 49);
  Layout cubeLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"edge","type":"rect","x":100,"y":50,"width":28,"height":14,"filled":true,"text":"","scale":1}]})",
      {}, 0, cubeLayout, error, 128, 64));
  Config cubeConfig;
  cubeConfig.layout = cubeLayout;
  Status cubeStatus;
  Canvas cubeFrame = render(cubeConfig, cubeStatus, true, 128, 64);
  assert(cubeFrame.at(100, 50) && cubeFrame.at(127, 63));
  {
    Canvas layered(128, 64);
    layered.set(0, 0);
    std::string zeros(1024, '\0');
    layered.blitPageMajor(0, 0, 128, 64, zeros, 0);
    assert(layered.at(0, 0));
    Layout overlayLayout;
    assert(parseLayout(
        R"({"version":1,"elements":[{"id":"hud","type":"rect","x":100,"y":50,"width":28,"height":14,"filled":true,"text":"","scale":1},{"id":"bg","type":"animation","x":0,"y":0,"width":128,"height":64,"asset":"clip.oan"}]})",
        {}, 0, overlayLayout, error, 128, 64));
    overlayLayout.elements[1].pixels.assign(1024, '\0');
    Config overlayConfig;
    overlayConfig.layout = overlayLayout;
    Canvas overlayFrame = render(overlayConfig, cubeStatus, true, 128, 64);
    assert(overlayFrame.at(100, 50) && overlayFrame.at(127, 63));
  }
  cubeConfig.rotation = 90;
  cubeConfig.layout.elements[0].x = 0;
  cubeConfig.layout.elements[0].y = 100;
  cubeConfig.layout.elements[0].width = 1;
  cubeConfig.layout.elements[0].height = 1;
  Canvas rotatedCubeFrame = render(cubeConfig, cubeStatus, true, 128, 64);
  assert(rotatedCubeFrame.at(27, 0));
  Json json;
  assert(
      parseJSON(R"({"text":"A\u0042","values":[1,true,null]})", json, error));
  assert(json.get("text") != nullptr && json.get("text")->string == "AB");

  const std::string configJSON = R"({
      "display_enabled":true,"burn_in_protection":true,"rotation":270,"sleep_seconds":300,
      "layout_json":"{\"version\":1,\"elements\":[{\"id\":\"host\",\"type\":\"text\",\"x\":2,\"y\":3,\"text\":\"{hostname}\",\"scale\":2,\"font_width\":3}]}"
    })";
  const std::string path = "/tmp/onekvm-oled-cpp-test.json";
  {
    std::ofstream output(path);
    output << configJSON;
  }
  Config config;
  std::string raw;
  if (!loadConfig(path, config, raw, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  assert(config.rotation == 270 && config.layout.elements.size() == 1 &&
         config.layout.elements[0].scale == 2 &&
         config.layout.elements[0].fontWidth == 3);
  ::unlink(path.c_str());

  const std::string assetReferencePath = "/tmp/onekvm-oled-asset-reference-test.json";
  {
    std::ofstream output(assetReferencePath);
    output << R"({"layout_json":"{\"version\":1,\"elements\":[{\"id\":\"movie\",\"type\":\"animation\",\"asset\":\"clip.oan\"}]}"})";
  }
  bool referenced = false;
  error.clear();
  assert(configReferencesAsset(assetReferencePath, "clip.oan", referenced, error));
  assert(referenced);
  assert(configReferencesAsset(assetReferencePath, "unused.oan", referenced, error));
  assert(!referenced);
  ::unlink(assetReferencePath.c_str());

  {
    std::ofstream output(path);
    output << R"({"display_enabled":true,"burn_in_protection":true,"rotation":0,"sleep_seconds":300,"layout_json":""})";
  }
  Config pcieDefaultConfig;
  assert(loadConfig(path, pcieDefaultConfig, raw, error, 64, 32));
  assert(pcieDefaultConfig.layout.elements.size() == 8);
  assert(pcieDefaultConfig.layout.elements[0].id == "text2" &&
         pcieDefaultConfig.layout.elements[0].text == "{ip}" &&
         pcieDefaultConfig.layout.elements[1].mode == "hdmi" &&
         pcieDefaultConfig.layout.elements[2].mode == "live" &&
         pcieDefaultConfig.layout.elements[7].type == "rect" &&
         pcieDefaultConfig.layout.elements[7].text == "ONEKVM");
  Config cubeDefaultConfig;
  assert(loadConfig(path, cubeDefaultConfig, raw, error, 128, 64));
  assert(cubeDefaultConfig.layout.elements.size() == 10 &&
         cubeDefaultConfig.layout.elements[0].id == "text2" &&
         cubeDefaultConfig.layout.elements[0].name == "IP" &&
         cubeDefaultConfig.layout.elements[0].x == 73 &&
         cubeDefaultConfig.layout.elements[2].mode == "eth" &&
         cubeDefaultConfig.layout.elements[3].mode == "hdmi" &&
         cubeDefaultConfig.layout.elements[4].mode == "live" &&
         cubeDefaultConfig.layout.elements[5].mode == "hid" &&
         cubeDefaultConfig.layout.elements[6].text == "{hostname}" &&
         cubeDefaultConfig.layout.elements[6].name == "Hostname" &&
         cubeDefaultConfig.layout.elements[8].text ==
             "{resolution} x {fps}HZ" &&
         cubeDefaultConfig.layout.elements[9].text == "{codec}");
  ::unlink(path.c_str());

  {
    std::ofstream output(path);
    output << R"({"display_enabled":true,"burn_in_protection":true,"rotation":0,"layout_json":""})";
  }
  Config defaultSleepConfig;
  assert(loadConfig(path, defaultSleepConfig, raw, error, 128, 64));
  assert(defaultSleepConfig.sleepSeconds == 3600);
  ::unlink(path.c_str());

  Layout graphicLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"custom","type":"graphic","x":0,"y":0,"mode":"custom","width":4,"height":1,"pixels":"A"},{"id":"eth","type":"graphic","x":10,"y":10,"mode":"eth","width":15,"height":7,"pixels":"000000000000000000000000000"},{"id":"hdmi","type":"graphic","x":35,"y":20,"mode":"hdmi","width":21,"height":7,"pixels":"0000000000000000000000000000000000000"}]})",
      {}, 0, graphicLayout, error));
  Config graphicConfig;
  graphicConfig.layout = graphicLayout;
  Status disconnected;
  Canvas disconnectedFrame = render(graphicConfig, disconnected);
  assert(disconnectedFrame.at(0, 0) && !disconnectedFrame.at(1, 0) &&
         disconnectedFrame.at(2, 0));
  assert(!disconnectedFrame.at(10, 11) && !disconnectedFrame.at(35, 21));
  Status connected;
  connected.ethernetConnected = true;
  connected.hdmiConnected = true;
  Canvas connectedFrame = render(graphicConfig, connected);
  assert(connectedFrame.at(10, 11) && connectedFrame.at(35, 21));

  Layout extendedStatusLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"hid","type":"graphic","x":0,"y":0,"mode":"hid","content":"text","text":"HID","scale":1,"filled":true,"bordered":false},{"id":"wifi","type":"graphic","x":10,"y":0,"mode":"wifi","content":"text","text":"WI-FI","scale":1,"filled":true,"bordered":false},{"id":"live","type":"graphic","x":20,"y":20,"mode":"live","content":"text","text":"LIVE","scale":1,"filled":true,"bordered":false}]})",
      {}, 0, extendedStatusLayout, error));
  Config extendedStatusConfig;
  extendedStatusConfig.layout = extendedStatusLayout;
  Status extendedStatus;
  extendedStatus.hidConnected = true;
  extendedStatus.wifiConnected = true;
  extendedStatus.active = true;
  Canvas liveDotOn = render(extendedStatusConfig, extendedStatus, true);
  Canvas liveDotOff = render(extendedStatusConfig, extendedStatus, false);
  assert(liveDotOn.at(0, 0) && liveDotOn.at(10, 0));
  assert(liveDotOn.at(45, 20) && liveDotOff.at(45, 20));
  assert(!liveDotOn.at(23, 21) && !liveDotOff.at(23, 21));
  assert(!liveDotOn.at(23, 23) && liveDotOff.at(23, 23));
  int changedPages = 0;
  int changedBytes = 0;
  for (int page = 0; page < 4; ++page) {
    const auto changed = changedColumns(liveDotOn, liveDotOff, page, 64);
    if (!changed.has_value())
      continue;
    ++changedPages;
    changedBytes += changed->second - changed->first + 1;
  }
  assert(changedPages <= 2 && changedBytes <= 6);
  assert(!changedColumns(liveDotOn, liveDotOn, 0, 64).has_value());
  extendedStatus.active = false;
  Canvas inactiveLive = render(extendedStatusConfig, extendedStatus, true);
  assert(!inactiveLive.at(20, 20) && !inactiveLive.at(45, 20) &&
         inactiveLive.at(23, 21));
  assert(hasLiveLayer(extendedStatusConfig));

  Layout adjustableLiveLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"compact","type":"graphic","x":0,"y":10,"mode":"live","content":"text","text":"I","scale":1,"font_width":3},{"id":"wide","type":"graphic","x":20,"y":10,"mode":"live","content":"text","text":"N","scale":1,"font_width":5}]})",
      {}, 0, adjustableLiveLayout, error));
  Config adjustableLiveConfig;
  adjustableLiveConfig.layout = adjustableLiveLayout;
  extendedStatus.active = true;
  Canvas adjustableLive = render(adjustableLiveConfig, extendedStatus, true);
  assert(adjustableLive.at(9, 10) && !adjustableLive.at(10, 10));
  assert(adjustableLive.at(31, 10) && !adjustableLive.at(32, 10));

  Layout statusLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"label","type":"graphic","x":2,"y":2,"mode":"eth","content":"text","text":"NET","scale":1,"filled":true,"bordered":false},{"id":"icon","type":"graphic","x":20,"y":2,"mode":"hdmi","content":"custom","width":2,"height":1,"pixels":"8","filled":false,"bordered":true}]})",
      {}, 0, statusLayout, error));
  Config statusConfig;
  statusConfig.layout = statusLayout;
  Canvas inactiveStatus = render(statusConfig, disconnected);
  assert(!inactiveStatus.at(2, 2) && inactiveStatus.at(3, 3));
  assert(!inactiveStatus.at(20, 2) && inactiveStatus.at(21, 3));
  Canvas activeStatus = render(statusConfig, connected);
  assert(activeStatus.at(2, 2) && !activeStatus.at(3, 3) &&
         activeStatus.at(4, 3));
  assert(activeStatus.at(20, 2) && activeStatus.at(21, 3));

  Layout fixedStatusLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"fixed","type":"graphic","x":0,"y":0,"mode":"eth","content":"text","text":"I","scale":1,"font_width":3,"filled":true,"bordered":false,"fixed_size":true,"width":12,"height":9}]})",
      {}, 0, fixedStatusLayout, error));
  assert(fixedStatusLayout.elements[0].fixedSize &&
         fixedStatusLayout.elements[0].width == 12 &&
         fixedStatusLayout.elements[0].height == 9);
  Config fixedStatusConfig;
  fixedStatusConfig.layout = fixedStatusLayout;
  Canvas fixedStatusFrame = render(fixedStatusConfig, connected);
  assert(fixedStatusFrame.at(11, 8));

  Layout alignedLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"right","type":"text","x":0,"y":0,"text":"A","scale":1,"width":12,"full_width":true,"align":"right"},{"id":"cn","type":"text","x":0,"y":8,"text":"中","scale":1}]})",
      {}, 0, alignedLayout, error));
  Config alignedConfig;
  alignedConfig.layout = alignedLayout;
  Canvas alignedFrame = render(alignedConfig, disconnected);
  assert(alignedFrame.at(62, 0) && !alignedFrame.at(1, 0));
  assert(alignedFrame.at(5, 8) && alignedFrame.at(6, 8));

  Layout rectangleTextLayout;
  assert(parseLayout(
      R"({"version":1,"elements":[{"id":"filled","type":"rect","x":0,"y":0,"width":9,"height":7,"filled":true,"text":"I","scale":1,"font_width":3,"align":"center"},{"id":"outlined","type":"rect","x":10,"y":0,"width":9,"height":7,"filled":false,"text":"N","scale":1,"font_width":5,"align":"center"}]})",
      {}, 0, rectangleTextLayout, error));
  Config rectangleTextConfig;
  rectangleTextConfig.layout = rectangleTextLayout;
  Canvas rectangleTextFrame = render(rectangleTextConfig, disconnected);
  assert(rectangleTextFrame.at(0, 0) && rectangleTextFrame.at(8, 6) &&
         !rectangleTextFrame.at(3, 1) && rectangleTextFrame.at(2, 1));
  assert(rectangleTextFrame.at(10, 0) && rectangleTextFrame.at(18, 6) &&
         rectangleTextFrame.at(12, 1) && rectangleTextFrame.at(16, 1));

  Layout invalidGraphic;
  error.clear();
  assert(!parseLayout(
      R"({"version":1,"elements":[{"id":"bad","type":"graphic","x":0,"y":0,"mode":"custom","width":4,"height":1,"pixels":""}]})",
      {}, 0, invalidGraphic, error));
  assert(error == "OLED graphic element is invalid");

  Layout invalidStatus;
  error.clear();
  assert(!parseLayout(
      R"({"version":1,"elements":[{"id":"bad","type":"graphic","x":0,"y":0,"mode":"eth","content":"image","text":"ETH","scale":1}]})",
      {}, 0, invalidStatus, error));
  assert(error == "OLED status graphic element is invalid");

  Status status;
  status.hostname = "nanokvm";
  Canvas frame = render(config, status);
  Canvas shifted = frame;
  shifted.shift(1, 1);
  assert(!(frame == shifted));

  std::string plan;
  assert(controllerPlan(
      R"({"api_version":1,"event":"reconcile","extension_id":"nanokvm-oled","enabled":true,"settings":{"display_enabled":true}})",
      plan, error));
  assert(plan.find("\"ensure\"") != std::string::npos);
  Json planJSON;
  assert(parseJSON(plan, planJSON, error));
  assert(controllerPlan(
      R"({"api_version":1,"event":"config_changed","extension_id":"nanokvm-oled","enabled":true,"settings":{"display_enabled":true}})",
      plan, error));
  assert(plan.find("\"remove\"") != std::string::npos &&
         plan.find("\"ensure\"") != std::string::npos &&
         plan.find("\"remove\"") < plan.find("\"ensure\""));
  assert(controllerPlan(
      R"({"api_version":1,"event":"reconcile","extension_id":"nanokvm-oled","enabled":true,"settings":{"display_enabled":false}})",
      plan, error));
  assert(plan.find("\"remove\"") != std::string::npos &&
         plan.find("\"--off\"") != std::string::npos);
  assert(controllerPlan(
      R"({"api_version":1,"event":"disable","extension_id":"nanokvm-oled","enabled":true,"settings":{"display_enabled":true}})",
      plan, error));
  assert(plan.find("\"remove\"") != std::string::npos &&
         plan.find("\"ensure\"") == std::string::npos);
  assert(controllerPlan(
      R"({"api_version":1,"event":"invoke","extension_id":"nanokvm-oled","enabled":false,"settings":{},"method":"off"})",
      plan, error));
  assert(plan.find("\"response\":{\"display\":\"off\"}") != std::string::npos);

  Status decoded;
  assert(decodeStatus(
	    R"({"uptime_seconds":3720,"video":{"active":true,"hdmi_connected":true,"codec":"h264","fps":60,"actual_fps":47,"resolution":0,"input_width":1920,"input_height":1080},"hid":{"available":true,"connected":true},"network":{"hostname":"nano","ip":"192.0.2.5","ethernet_connected":true,"wifi_available":false,"wifi_connected":false}})",
      decoded, error));
  assert(decoded.hdmiConnected && decoded.ethernetConnected &&
         decoded.hidAvailable && decoded.hidConnected &&
         !decoded.wifiAvailable && !decoded.wifiConnected);
  assert(expandTemplate("{ip} {codec} {fps} {stream} {uptime}", decoded) ==
	         "192.0.2.5 H264 47 STREAM ACTIVE 1H02M");
  assert(expandTemplate("{resolution}", decoded) == "1920x1080");
  decoded.cpuPercent = 12;
  decoded.memoryUsed = 18ull * 1024 * 1024;
  decoded.memoryTotal = 32ull * 1024 * 1024;
  decoded.storageAvailable = 8ull * 1024 * 1024 * 1024;
  decoded.storageTotal = 16ull * 1024 * 1024 * 1024;
  assert(expandTemplate("{cpu} {mem} {disk}", decoded) == "{cpu} {mem} {disk}");
  assert(expandTemplate("{cpu_used} {cpu_free} {cpu_available} {cpu_percent}",
                        decoded) == "12% 88% 88% 12%");
  assert(expandTemplate("{mem_used} {mem_free} {mem_available} {mem_percent}",
                        decoded) == "18M 14M 14M 56%");
  assert(expandTemplate(
             "{disk_used} {disk_free} {disk_available} {disk_percent}",
             decoded) == "8G 8G 8G 50%");
  decoded.cpuPercent = -1;
  decoded.memoryTotal = 0;
  decoded.storageTotal = 0;
  assert(expandTemplate(
             "{cpu_used} {cpu_free} {cpu_available} {cpu_percent} "
             "{mem_used} {mem_free} {mem_available} {mem_percent} "
             "{disk_used} {disk_free} {disk_available} {disk_percent}",
             decoded) == "-- -- -- -- -- -- -- -- -- -- -- --");
  std::map<std::string, std::string> variables{{"c_room", "A1"}};
  assert(expandTemplate("{c_room} {ip}", decoded, variables) == "A1 192.0.2.5");

  {
    std::ofstream output(path);
    output << R"({"display_enabled":true,"burn_in_protection":true,"rotation":0,"sleep_seconds":300,"layout_json":"","variables_json":"{\"c_room\":\"lab\"}"})";
  }
  Config withVariables;
  error.clear();
  assert(loadConfig(path, withVariables, raw, error, 128, 64));
  assert(withVariables.variables["c_room"] == "lab");
  {
    std::ofstream output(path);
    output << R"({"display_enabled":true,"burn_in_protection":true,"rotation":0,"sleep_seconds":300,"layout_json":"","variables_json":"{\"room\":\"lab\"}"})";
  }
  error.clear();
  assert(!loadConfig(path, withVariables, raw, error, 128, 64));
  assert(error.find("c_") != std::string::npos);
  ::unlink(path.c_str());
  return 0;
}
