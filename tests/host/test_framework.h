#pragma once

// 极简测试框架：不引入任何第三方依赖，便于在主机上直接编译运行。

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& Registry() {
  static std::vector<Case> registry;
  return registry;
}

inline int& Failures() {
  static int failures = 0;
  return failures;
}

inline int& Checks() {
  static int checks = 0;
  return checks;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) {
    Registry().push_back({name, fn});
  }
};

template <typename T>
inline std::string ToString(const T& value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

inline std::string ToString(bool value) { return value ? "true" : "false"; }

inline void ReportFailure(const char* file, int line, const std::string& what) {
  ++Failures();
  std::printf("  \033[31mFAIL\033[0m %s:%d\n       %s\n", file, line,
              what.c_str());
}

inline int RunAll() {
  int failed_cases = 0;
  for (const Case& c : Registry()) {
    const int before = Failures();
    std::printf("\033[1m[ RUN  ]\033[0m %s\n", c.name);
    c.fn();
    if (Failures() == before) {
      std::printf("\033[32m[  OK  ]\033[0m %s\n", c.name);
    } else {
      ++failed_cases;
      std::printf("\033[31m[ FAIL ]\033[0m %s\n", c.name);
    }
  }
  std::printf("\n----------------------------------------\n");
  std::printf("用例: %zu 个，失败: %d 个，断言: %d 条\n", Registry().size(),
              failed_cases, Checks());
  return failed_cases == 0 ? 0 : 1;
}

}  // namespace test

#define TEST_CASE(name)                                    \
  static void name();                                      \
  static ::test::Registrar registrar_##name(#name, &name); \
  static void name()

#define CHECK(cond)                                                  \
  do {                                                               \
    ++::test::Checks();                                              \
    if (!(cond)) {                                                   \
      ::test::ReportFailure(__FILE__, __LINE__, "断言失败: " #cond); \
    }                                                                \
  } while (false)

#define CHECK_EQ(actual, expected)                                            \
  do {                                                                        \
    ++::test::Checks();                                                       \
    const auto a_ = (actual);                                                 \
    const auto e_ = (expected);                                               \
    if (!(a_ == e_)) {                                                        \
      ::test::ReportFailure(__FILE__, __LINE__,                               \
                            std::string("期望 ") + #actual " == " #expected + \
                                "\n       实际: [" + ::test::ToString(a_) +   \
                                "]" + "\n       期望: [" +                    \
                                ::test::ToString(e_) + "]");                  \
    }                                                                         \
  } while (false)
