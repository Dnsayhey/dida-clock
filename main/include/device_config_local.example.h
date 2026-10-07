#pragma once

// 开发默认配置模板。
//
// 用法：
//   cp main/include/device_config_local.example.h main/include/device_config_local.h
//   然后填入你自己的 Wi-Fi 与位置。
//
// device_config_local.h 已被 .gitignore 忽略，不会进入版本历史
// （密钥明文一旦提交就会永久留在历史里，即便之后删掉也无济于事）。
//
// 留空或未定义某项时，该项会被跳过；已存在于 NVS 的字段不会被覆盖。

#define DEV_WIFI_SSID "your-wifi-ssid"
#define DEV_WIFI_PASSWORD "your-wifi-password"

// 和风天气的城市查询参数：adm 是上级区域，location 是具体区县
#define DEV_ADM "杭州"
#define DEV_LOCATION "余杭"
