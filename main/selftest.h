#pragma once

namespace selftest {

// 启动自检：验证 ROM miniz 的 gzip 解压链路。
// 返回 false 表示解压失败或结果内容不符预期。
bool RunDecompressSelfTest();

}  // namespace selftest
