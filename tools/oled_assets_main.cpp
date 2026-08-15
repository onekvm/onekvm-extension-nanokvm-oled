#include "../src/assets.hpp"
#include <atomic>
#include <cstdlib>
#include <string>
#include <signal.h>
#include <sys/resource.h>

namespace onekvm::oled { std::atomic_bool stopped = false; }
static void stop(int) { onekvm::oled::stopped.store(true); }
int main() {
  (void)::setpriority(PRIO_PROCESS, 0, 19);
  struct sigaction action{}; action.sa_handler = stop; sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr); sigaction(SIGTERM, &action, nullptr);
  const char *socket = std::getenv("ONEKVM_EXTENSION_SOCKET");
  const char *data = std::getenv("ONEKVM_EXTENSION_DATA");
  if (!socket || !data) return 2;
  std::string error;
  return onekvm::oled::runAssetServer(std::string(socket) + "/assets.sock", std::string(data) + "/assets", error) ? 0 : 1;
}
