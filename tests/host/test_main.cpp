// 主机单测入口。
//
// 这些测试针对的是**纯逻辑**部分（JSON 解析、URL 编码、WeatherStore 并发语义），
// 它们不依赖 ESP-IDF，因此可以在开发机上直接跑，不需要开发板。

#include <cstdio>

#include "test_framework.h"

int main() {
  std::printf(
      "dida-clock 主机单测：weather 解析 / URL 编码 / WeatherStore 并发\n");
  std::printf(
      "============================================================\n\n");
  const int rc = test::RunAll();
  if (rc == 0) {
    std::printf("\033[32m全部通过\033[0m\n");
  } else {
    std::printf("\033[31m存在失败\033[0m\n");
  }
  return rc;
}
