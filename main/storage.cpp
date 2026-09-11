#include "storage.hpp"

#include <cstdio>
#include <fstream>

#include "file_system.hpp"
#include "logger.hpp"

namespace {
espp::Logger logger({.tag = "storage", .level = espp::Logger::Verbosity::INFO});
} // namespace

std::string storage_path(std::string_view name) {
  espp::FileSystem::get(); // mounts on first use
  return (espp::FileSystem::get_root_path() / std::filesystem::path(name)).string();
}

bool storage_write(std::string_view name, std::string_view contents) {
  const std::string path = storage_path(name);
  const std::string temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.flush();
    if (!out) {
      logger.error("could not write {}", temp);
      return false;
    }
  }
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    logger.error("could not rename {} to {}", temp, path);
    return false;
  }
  return true;
}
