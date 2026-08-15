#include "assets.hpp"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>

namespace onekvm::oled {
extern std::atomic_bool stopped;

namespace {
constexpr std::size_t maxHeaderBytes = 64U * 1024U;
constexpr std::size_t maxAssetBytes = 32U * 1024U * 1024U;

std::string configPath() {
  const char *directory = std::getenv("ONEKVM_EXTENSION_CONFIG");
  return directory != nullptr ? std::string(directory) + "/config.json"
                              : "/run/onekvm/extensions/nanokvm-oled/config.json";
}

struct Request {
  std::string method;
  std::string target;
  std::unordered_map<std::string, std::string> headers;
  std::string bufferedBody;
};

class FileLock {
public:
  FileLock(const std::filesystem::path &path, int operation)
      : fd_(::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600)) {
    if (fd_ >= 0 && ::flock(fd_, operation) == 0)
      locked_ = true;
  }

  ~FileLock() {
    if (locked_)
      (void)::flock(fd_, LOCK_UN);
    if (fd_ >= 0)
      ::close(fd_);
  }

  explicit operator bool() const { return locked_; }

private:
  int fd_ = -1;
  bool locked_ = false;
};

std::string base64(std::string_view input) {
  static constexpr char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve((input.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < input.size(); i += 3) {
    const uint32_t value =
        static_cast<unsigned char>(input[i]) << 16 |
        (i + 1 < input.size()
             ? static_cast<unsigned char>(input[i + 1]) << 8
             : 0) |
        (i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0);
    output += table[(value >> 18) & 63];
    output += table[(value >> 12) & 63];
    output += i + 1 < input.size() ? table[(value >> 6) & 63] : '=';
    output += i + 2 < input.size() ? table[value & 63] : '=';
  }
  return output;
}

std::string jsonEscape(std::string_view value) {
  std::string output = "\"";
  for (char c : value) {
    if (c == '\\' || c == '"')
      output += '\\';
    output += c;
  }
  output += '"';
  return output;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return value;
}

std::string trim(std::string value) {
  const auto whitespace = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!value.empty() && whitespace(value.front()))
    value.erase(value.begin());
  while (!value.empty() && whitespace(value.back()))
    value.pop_back();
  return value;
}

bool safeName(std::string_view name) {
  return name.size() > 4 && name.size() <= 96 && name.ends_with(".oan") &&
         std::all_of(name.begin(), name.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
         });
}

bool sendAll(int fd, const char *data, std::size_t size) {
  while (size > 0) {
    const ssize_t count = ::send(fd, data, size, MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return false;
    data += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

void response(int fd, std::string_view status, std::string_view type,
              std::string_view body) {
  std::ostringstream header;
  header << "HTTP/1.1 " << status << "\r\nContent-Type: " << type
         << "\r\nContent-Length: " << body.size()
         << "\r\nConnection: close\r\n\r\n";
  const std::string text = header.str();
  if (sendAll(fd, text.data(), text.size()))
    (void)sendAll(fd, body.data(), body.size());
}

void fileResponse(int fd, const std::filesystem::path &path,
                  std::uintmax_t size) {
  std::ostringstream header;
  header << "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
         << "Content-Length: " << size << "\r\nConnection: close\r\n\r\n";
  const std::string text = header.str();
  if (!sendAll(fd, text.data(), text.size()))
    return;
  std::ifstream file(path, std::ios::binary);
  std::array<char, 16U * 1024U> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = file.gcount();
    if (count > 0 &&
        !sendAll(fd, buffer.data(), static_cast<std::size_t>(count)))
      return;
  }
}

bool readRequest(int fd, Request &result, std::string &error) {
  std::string input;
  std::array<char, 4096> buffer{};
  std::size_t headerEnd = std::string::npos;
  while ((headerEnd = input.find("\r\n\r\n")) == std::string::npos) {
    if (input.size() >= maxHeaderBytes) {
      error = "request headers are too large";
      return false;
    }
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      error = "incomplete request headers";
      return false;
    }
    input.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (headerEnd > maxHeaderBytes) {
    error = "request headers are too large";
    return false;
  }

  const std::size_t firstLineEnd = input.find("\r\n");
  const std::string firstLine = input.substr(0, firstLineEnd);
  const std::size_t firstSpace = firstLine.find(' ');
  const std::size_t secondSpace = firstLine.find(' ', firstSpace + 1);
  if (firstLineEnd == std::string::npos || firstSpace == std::string::npos ||
      secondSpace == std::string::npos) {
    error = "invalid request line";
    return false;
  }
  result.method = firstLine.substr(0, firstSpace);
  result.target = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);

  std::size_t position = firstLineEnd + 2;
  while (position < headerEnd) {
    const std::size_t end = input.find("\r\n", position);
    if (end == std::string::npos || end > headerEnd) {
      error = "invalid request header";
      return false;
    }
    const std::string line = input.substr(position, end - position);
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
      error = "invalid request header";
      return false;
    }
    result.headers[lower(trim(line.substr(0, colon)))] =
        trim(line.substr(colon + 1));
    position = end + 2;
  }
  result.bufferedBody = input.substr(headerEnd + 4);
  return true;
}

bool readMore(int fd, std::string &buffer, std::string &error) {
  std::array<char, 8192> incoming{};
  while (true) {
    const ssize_t count = ::read(fd, incoming.data(), incoming.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      error = "incomplete request body";
      return false;
    }
    buffer.append(incoming.data(), static_cast<std::size_t>(count));
    return true;
  }
}

bool writeBytes(std::ofstream &file, const char *data, std::size_t size,
                std::size_t &total, std::string &error) {
  if (size > maxAssetBytes - total) {
    error = "asset is too large";
    return false;
  }
  file.write(data, static_cast<std::streamsize>(size));
  if (!file) {
    error = "could not write asset";
    return false;
  }
  total += size;
  return true;
}

bool readFixedBody(int fd, std::string buffer, std::size_t contentLength,
                   std::ofstream &file, std::size_t &total,
                   std::string &error) {
  if (contentLength > maxAssetBytes) {
    error = "asset is too large";
    return false;
  }
  if (buffer.size() > contentLength)
    buffer.resize(contentLength);
  if (!writeBytes(file, buffer.data(), buffer.size(), total, error))
    return false;
  std::array<char, 16U * 1024U> incoming{};
  while (total < contentLength) {
    const std::size_t wanted = std::min(incoming.size(), contentLength - total);
    const ssize_t count = ::read(fd, incoming.data(), wanted);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      error = "incomplete request body";
      return false;
    }
    if (!writeBytes(file, incoming.data(), static_cast<std::size_t>(count),
                    total, error))
      return false;
  }
  return true;
}

bool readChunkedBody(int fd, std::string buffer, std::ofstream &file,
                     std::size_t &total, std::string &error) {
  while (true) {
    std::size_t lineEnd = buffer.find("\r\n");
    while (lineEnd == std::string::npos) {
      if (buffer.size() > 1024) {
        error = "invalid chunk header";
        return false;
      }
      if (!readMore(fd, buffer, error))
        return false;
      lineEnd = buffer.find("\r\n");
    }

    std::string sizeText = buffer.substr(0, lineEnd);
    const std::size_t extension = sizeText.find(';');
    if (extension != std::string::npos)
      sizeText.resize(extension);
    sizeText = trim(sizeText);
    std::size_t chunkSize = 0;
    const auto parsed = std::from_chars(sizeText.data(),
                                        sizeText.data() + sizeText.size(),
                                        chunkSize, 16);
    if (sizeText.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != sizeText.data() + sizeText.size() ||
        chunkSize > maxAssetBytes - total) {
      error = "invalid chunk size";
      return false;
    }
    buffer.erase(0, lineEnd + 2);
    if (chunkSize == 0)
      return true;

    std::size_t remaining = chunkSize;
    while (remaining > 0) {
      if (buffer.empty() && !readMore(fd, buffer, error))
        return false;
      const std::size_t count = std::min(buffer.size(), remaining);
      if (!writeBytes(file, buffer.data(), count, total, error))
        return false;
      buffer.erase(0, count);
      remaining -= count;
    }
    while (buffer.size() < 2 && readMore(fd, buffer, error)) {
    }
    if (buffer.size() < 2 || buffer.compare(0, 2, "\r\n") != 0) {
      error = "invalid chunk terminator";
      return false;
    }
    buffer.erase(0, 2);
  }
}

uint16_t little16(const std::array<char, 14> &header, std::size_t at) {
  return static_cast<uint16_t>(static_cast<unsigned char>(header[at]) |
                               static_cast<unsigned char>(header[at + 1]) << 8);
}

uint32_t little32(const std::array<char, 14> &header, std::size_t at) {
  return static_cast<uint32_t>(
      static_cast<unsigned char>(header[at]) |
      static_cast<unsigned char>(header[at + 1]) << 8 |
      static_cast<unsigned char>(header[at + 2]) << 16 |
      static_cast<unsigned char>(header[at + 3]) << 24);
}

std::string durationJSON(uint32_t frames, uint16_t fps) {
  const uint32_t seconds = frames / fps;
  const uint32_t hundredths = (frames % fps) * 100U / fps;
  std::string value = std::to_string(seconds) + ".";
  if (hundredths < 10)
    value += '0';
  value += std::to_string(hundredths);
  return value;
}

bool validAssetFile(const std::filesystem::path &path, std::uintmax_t size,
                    std::array<char, 14> &header) {
  if (size < header.size())
    return false;
  std::ifstream file(path, std::ios::binary);
  file.read(header.data(), static_cast<std::streamsize>(header.size()));
  if (!file || std::string_view(header.data(), 4) != "OAN1")
    return false;
  const uint16_t width = little16(header, 4);
  const uint16_t height = little16(header, 6);
  const uint16_t fps = little16(header, 8);
  const uint32_t frames = little32(header, 10);
  if (width < 8 || width > 256 || height < 8 || height > 256 ||
      height % 8 != 0 || fps == 0 || fps > 120 || frames == 0)
    return false;
  const std::uintmax_t frameBytes =
      static_cast<std::uintmax_t>(width) * height / 8;
  return frames <= (maxAssetBytes - header.size()) / frameBytes &&
         size == header.size() + static_cast<std::uintmax_t>(frames) * frameBytes;
}

void listAssets(int fd, const std::filesystem::path &root) {
  // The data directory may be provisioned read-only by the package manager;
  // keep the coordination file beside it, in the extension-owned app-data
  // directory, instead of requiring a hidden file in the asset collection.
  FileLock lock(root.parent_path() / ".assets.lock", LOCK_SH);
  if (!lock) {
    response(fd, "503 Service Unavailable", "text/plain", "asset store busy");
    return;
  }
  std::string output = "[";
  bool first = true;
  DIR *directory = ::opendir(root.c_str());
  if (directory == nullptr) {
    response(fd, "500 Internal Server Error", "text/plain",
             "could not read asset store");
    return;
  }
  while (const dirent *entry = ::readdir(directory)) {
    const std::string filename(entry->d_name);
    if (!safeName(filename))
      continue;
    const std::filesystem::path path = root / filename;
    struct stat information{};
    if (::stat(path.c_str(), &information) < 0 || !S_ISREG(information.st_mode))
      continue;
    const std::uintmax_t size = static_cast<std::uintmax_t>(information.st_size);
    std::array<char, 14> header{};
    if (!validAssetFile(path, size, header))
      continue;
    const uint16_t width = little16(header, 4);
    const uint16_t height = little16(header, 6);
    const uint16_t fps = little16(header, 8);
    const uint32_t frames = little32(header, 10);
    const std::size_t frameBytes = static_cast<std::size_t>(width) * height / 8;
    std::string preview(frameBytes, '\0');
    std::ifstream file(path, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(header.size()));
    file.read(preview.data(), static_cast<std::streamsize>(preview.size()));
    if (!file)
      continue;
    if (!first)
      output += ',';
    first = false;
    output += "{\"id\":" + jsonEscape(filename) +
              ",\"name\":" + jsonEscape(filename.substr(0, filename.size() - 4)) +
              ",\"width\":" + std::to_string(width) +
              ",\"height\":" + std::to_string(height) +
              ",\"fps\":" + std::to_string(fps) +
              ",\"frames\":" + std::to_string(frames) +
              ",\"duration_seconds\":" + durationJSON(frames, fps) +
              ",\"size\":" + std::to_string(size) +
              ",\"preview_base64\":" + jsonEscape(base64(preview)) + "}";
  }
  ::closedir(directory);
  output += ']';
  response(fd, "200 OK", "application/json", output);
}

void getAsset(int fd, const std::filesystem::path &root,
              std::string_view target) {
  const std::string name(target.substr(1));
  if (!safeName(name)) {
    response(fd, "400 Bad Request", "text/plain", "invalid name");
    return;
  }
  FileLock lock(root.parent_path() / ".assets.lock", LOCK_SH);
  if (!lock) {
    response(fd, "503 Service Unavailable", "text/plain", "asset store busy");
    return;
  }
  std::error_code error;
  const std::filesystem::path path = root / name;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  std::array<char, 14> header{};
  if (error || !validAssetFile(path, size, header)) {
    response(fd, "404 Not Found", "text/plain", "not found");
    return;
  }
  fileResponse(fd, path, size);
}

void deleteAsset(int fd, const std::filesystem::path &root,
                 std::string_view target) {
  const std::string name(target.substr(1));
  if (!safeName(name)) {
    response(fd, "400 Bad Request", "text/plain", "invalid name");
    return;
  }
  bool referenced = false;
  std::string referenceError;
  if (!configReferencesAsset(configPath(), name, referenced, referenceError)) {
    response(fd, "503 Service Unavailable", "text/plain",
             "could not verify canvas references: " + referenceError);
    return;
  }
  if (referenced) {
    response(fd, "409 Conflict", "application/json",
             "{\"error\":\"asset_in_use\"}");
    return;
  }
  FileLock lock(root.parent_path() / ".assets.lock", LOCK_EX);
  if (!lock) {
    response(fd, "503 Service Unavailable", "text/plain", "asset store busy");
    return;
  }
  std::error_code fileError;
  const auto path = root / name;
  const auto size = std::filesystem::file_size(path, fileError);
  std::array<char, 14> header{};
  if (fileError || !validAssetFile(path, size, header)) {
    response(fd, "404 Not Found", "text/plain", "not found");
    return;
  }
  std::error_code removeError;
  if (!std::filesystem::remove(path, removeError) || removeError) {
    response(fd, "500 Internal Server Error", "text/plain",
             removeError ? removeError.message() : "could not delete asset");
    return;
  }
  response(fd, "200 OK", "application/json", "{\"status\":\"deleted\"}");
}

void renameAsset(int fd, const std::filesystem::path &root,
                 const Request &request) {
  const std::string prefix = "/rename?name=";
  const std::string separator = "&new_name=";
  const std::size_t separatorAt = request.target.find(separator, prefix.size());
  if (separatorAt == std::string::npos) {
    response(fd, "400 Bad Request", "text/plain", "invalid rename request");
    return;
  }
  const std::string oldName =
      request.target.substr(prefix.size(), separatorAt - prefix.size());
  const std::string newName = request.target.substr(separatorAt + separator.size());
  if (!safeName(oldName) || !safeName(newName)) {
    response(fd, "400 Bad Request", "text/plain", "invalid name");
    return;
  }
  bool referenced = false;
  std::string referenceError;
  if (!configReferencesAsset(configPath(), oldName, referenced, referenceError)) {
    response(fd, "503 Service Unavailable", "text/plain",
             "could not verify canvas references: " + referenceError);
    return;
  }
  if (referenced) {
    response(fd, "409 Conflict", "application/json",
             "{\"error\":\"asset_in_use\"}");
    return;
  }
  FileLock lock(root.parent_path() / ".assets.lock", LOCK_EX);
  if (!lock) {
    response(fd, "503 Service Unavailable", "text/plain", "asset store busy");
    return;
  }
  const auto oldPath = root / oldName;
  const auto newPath = root / newName;
  std::error_code fileError;
  const auto size = std::filesystem::file_size(oldPath, fileError);
  std::array<char, 14> header{};
  if (fileError || !validAssetFile(oldPath, size, header)) {
    response(fd, "404 Not Found", "text/plain", "not found");
    return;
  }
  if (oldName == newName) {
    response(fd, "200 OK", "application/json", "{\"status\":\"renamed\"}");
    return;
  }
  if (std::filesystem::exists(newPath, fileError) || fileError) {
    response(fd, fileError ? "500 Internal Server Error" : "409 Conflict",
             fileError ? "text/plain" : "application/json",
             fileError ? fileError.message() : "{\"error\":\"asset_exists\"}");
    return;
  }
  std::filesystem::rename(oldPath, newPath, fileError);
  if (fileError) {
    response(fd, "500 Internal Server Error", "text/plain", fileError.message());
    return;
  }
  response(fd, "200 OK", "application/json", "{\"status\":\"renamed\"}");
}

void uploadAsset(int fd, const std::filesystem::path &root,
                 const Request &request) {
  const std::string name = request.target.substr(13);
  if (!safeName(name)) {
    response(fd, "400 Bad Request", "text/plain", "invalid name");
    return;
  }
  const auto transfer = request.headers.find("transfer-encoding");
  const bool chunked = transfer != request.headers.end() &&
                       lower(transfer->second).find("chunked") != std::string::npos;
  std::size_t contentLength = 0;
  if (!chunked) {
    const auto length = request.headers.find("content-length");
    if (length == request.headers.end()) {
      response(fd, "411 Length Required", "text/plain",
               "content length is required");
      return;
    }
    const std::string value = trim(length->second);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                        contentLength, 10);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size()) {
      response(fd, "400 Bad Request", "text/plain", "invalid content length");
      return;
    }
    if (contentLength > maxAssetBytes) {
      response(fd, "413 Content Too Large", "text/plain", "asset is too large");
      return;
    }
  }

  FileLock lock(root.parent_path() / ".assets.lock", LOCK_EX);
  if (!lock) {
    response(fd, "503 Service Unavailable", "text/plain", "asset store busy");
    return;
  }
  const std::filesystem::path temporary = root / (name + ".partial");
  std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
  std::size_t total = 0;
  std::string error;
  if (!file)
    error = "could not write asset";
  const auto expect = request.headers.find("expect");
  if (file && expect != request.headers.end() &&
      lower(expect->second).find("100-continue") != std::string::npos) {
    static constexpr std::string_view continueResponse =
        "HTTP/1.1 100 Continue\r\n\r\n";
    if (!sendAll(fd, continueResponse.data(), continueResponse.size()))
      error = "client disconnected";
  }
  bool success = error.empty() &&
                 (chunked ? readChunkedBody(fd, request.bufferedBody, file,
                                            total, error)
                          : readFixedBody(fd, request.bufferedBody, contentLength,
                                          file, total, error));
  file.flush();
  success = success && file.good();
  file.close();

  std::array<char, 14> header{};
  success = success && validAssetFile(temporary, total, header);
  std::error_code renameError;
  if (success)
    std::filesystem::rename(temporary, root / name, renameError);
  if (!success || renameError) {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    if (renameError || error == "could not write asset")
      response(fd, "500 Internal Server Error", "text/plain",
               renameError ? renameError.message() : error);
    else if (error == "asset is too large")
      response(fd, "413 Content Too Large", "text/plain", error);
    else if (!error.empty())
      response(fd, "400 Bad Request", "text/plain", error);
    else
      response(fd, "400 Bad Request", "text/plain", "invalid asset");
    return;
  }
  response(fd, "201 Created", "application/json", "{\"status\":\"ok\"}");
}

void requestPlayback(int fd, const std::filesystem::path &root,
                     const Request &request) {
  const std::string prefix = "/play?name=";
  const std::string name = request.target.substr(prefix.size());
  if (!safeName(name)) {
    response(fd, "400 Bad Request", "text/plain", "invalid name");
    return;
  }
  FileLock lock(root.parent_path() / ".assets.lock", LOCK_SH);
  if (!lock) {
    response(fd, "503 Service Unavailable", "text/plain", "asset store busy");
    return;
  }
  std::error_code fileError;
  const auto path = root / name;
  const auto size = std::filesystem::file_size(path, fileError);
  std::array<char, 14> header{};
  if (fileError || !validAssetFile(path, size, header)) {
    response(fd, "404 Not Found", "text/plain", "not found");
    return;
  }
  const auto requestPath = root.parent_path() / ".play.request";
  const auto temporary = root.parent_path() / ".play.request.partial";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file || !(file << name) || !file.flush()) {
      response(fd, "500 Internal Server Error", "text/plain",
               "could not queue playback");
      return;
    }
  }
  std::error_code renameError;
  std::filesystem::rename(temporary, requestPath, renameError);
  if (renameError) {
    std::filesystem::remove(temporary);
    response(fd, "500 Internal Server Error", "text/plain",
             "could not queue playback");
    return;
  }
  response(fd, "202 Accepted", "application/json", "{\"status\":\"queued\"}");
}

void handle(int fd, const std::filesystem::path &root) {
  Request request;
  std::string error;
  if (!readRequest(fd, request, error)) {
    response(fd, "400 Bad Request", "text/plain", error);
    return;
  }
  if (request.method == "GET" &&
      (request.target == "/" || request.target.empty())) {
    listAssets(fd, root);
    return;
  }
  if (request.method == "GET" && request.target.size() > 1 &&
      request.target.front() == '/') {
    getAsset(fd, root, request.target);
    return;
  }
  if (request.method == "DELETE" && request.target.size() > 1 &&
      request.target.front() == '/') {
    deleteAsset(fd, root, request.target);
    return;
  }
  if (request.method == "POST" &&
      request.target.rfind("/upload?name=", 0) == 0) {
    uploadAsset(fd, root, request);
    return;
  }
  if (request.method == "POST" &&
      request.target.rfind("/rename?name=", 0) == 0) {
    renameAsset(fd, root, request);
    return;
  }
  if (request.method == "POST" && request.target.rfind("/play?name=", 0) == 0) {
    requestPlayback(fd, root, request);
    return;
  }
  response(fd, "404 Not Found", "text/plain", "not found");
}
} // namespace

bool configReferencesAsset(const std::string &path, std::string_view assetName,
                           bool &referenced, std::string &error) {
  referenced = false;
  std::error_code existsError;
  if (!std::filesystem::exists(path, existsError)) {
    if (existsError) {
      error = existsError.message();
      return false;
    }
    return true;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "could not read OLED config";
    return false;
  }
  const std::string raw{std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>()};
  Json settings;
  if (!parseJSON(raw, settings, error) || settings.type != Json::Type::Object)
    return false;
  const Json *layoutValue = settings.get("layout_json");
  if (layoutValue == nullptr)
    return true;
  if (layoutValue->type != Json::Type::String) {
    error = "OLED layout_json is not a string";
    return false;
  }
  if (layoutValue->string.empty())
    return true;
  Json layout;
  if (!parseJSON(layoutValue->string, layout, error) ||
      layout.type != Json::Type::Object)
    return false;
  const Json *elements = layout.get("elements");
  if (elements == nullptr)
    return true;
  if (elements->type != Json::Type::Array) {
    error = "OLED layout elements is not an array";
    return false;
  }
  for (const Json &element : elements->array) {
    if (element.type != Json::Type::Object)
      continue;
    const Json *type = element.get("type");
    const Json *asset = element.get("asset");
    if (type != nullptr && type->type == Json::Type::String &&
        type->string == "animation" && asset != nullptr &&
        asset->type == Json::Type::String && asset->string == assetName) {
      referenced = true;
      return true;
    }
  }
  return true;
}

bool runAssetServer(const std::string &socketPath, const std::string &dataPath,
                    std::string &error) {
  std::error_code filesystemError;
  std::filesystem::create_directories(
      std::filesystem::path(socketPath).parent_path(), filesystemError);
  if (!filesystemError)
    std::filesystem::create_directories(dataPath, filesystemError);
  if (filesystemError) {
    error = filesystemError.message();
    return false;
  }
  ::unlink(socketPath.c_str());
  const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server < 0) {
    error = std::strerror(errno);
    return false;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (socketPath.size() >= sizeof(address.sun_path)) {
    error = "asset socket path too long";
    ::close(server);
    return false;
  }
  std::strncpy(address.sun_path, socketPath.c_str(),
               sizeof(address.sun_path) - 1);
  if (::bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
          0 ||
      ::listen(server, 4) < 0) {
    error = std::strerror(errno);
    ::close(server);
    ::unlink(socketPath.c_str());
    return false;
  }
  // The API server joins onekvm-extension, so the socket group needs write
  // permission as well as read/execute permission.  The service umask would
  // otherwise produce mode 0750, which makes connect(2) fail with EPERM.
  if (::chmod(socketPath.c_str(), 0660) < 0) {
    error = std::strerror(errno);
    ::close(server);
    ::unlink(socketPath.c_str());
    return false;
  }
  while (!stopped.load()) {
    const int client = ::accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    handle(client, dataPath);
    ::close(client);
  }
  ::close(server);
  ::unlink(socketPath.c_str());
  return true;
}
} // namespace onekvm::oled
