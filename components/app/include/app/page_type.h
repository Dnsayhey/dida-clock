#pragma once

namespace app {

// 页面标识。
//
// 放在 app 层而不是 pages 层，原因是**导航决策属于应用层**：
// "单击在天气页之间切换、双击进设置页" 这条规则由 AppController 决定，
// 它需要引用页面标识。若把它放在 pages 层，就会出现 app ↔ pages 循环依赖。
//
// 页面的实现（widget、布局）在 pages 层；这里只是标识符。
enum class PageType {
  kInit = 0,
  kNetworkSetup,
  kRealTimeWeather,
  kFutureWeather,
  kThemeSettings,
  kFactoryReset,

  kCount,  // 哨兵，便于遍历与断言
};

const char* PageTypeName(PageType page);

}  // namespace app
