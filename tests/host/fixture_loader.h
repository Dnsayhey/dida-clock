#pragma once

// 测试用的小工具：读文件、读 fixture。

#include <fstream>
#include <sstream>
#include <string>

namespace fixtures {

inline std::string LoadBinary(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

inline std::string Path(const std::string& name) {
  return std::string(FIXTURES_DIR) + "/" + name;
}

inline std::string Load(const std::string& name) {
  return LoadBinary(Path(name));
}

}  // namespace fixtures
