idf_component_register(
    SRCS "portal_page.cpp" "dns_message.cpp" "dns_hijack.cpp" "captive_portal.cpp"
         "portal_http.cpp" "portal_wifi_scan.cpp"
    INCLUDE_DIRS "include"
    # portal_page.cpp 是纯逻辑（无 ESP 依赖），参与主机测试
    REQUIRES storage esp_wifi esp_netif esp_http_server lwip esp_hw_support
)
