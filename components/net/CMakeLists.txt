idf_component_register(
    # time_zone.cpp 是**纯逻辑**（不含 ESP 依赖），因此也在
    # tests/host/run_tests.sh 的 SOURCES 里 —— 它必须保持可被主机单测。
    SRCS "wifi_driver.cpp" "wifi_station.cpp" "http_client.cpp" "sntp.cpp" "network_status.cpp"
         "time_zone.cpp"
    INCLUDE_DIRS "include"
    # mbedtls：为了 esp_crt_bundle.h（HTTPS 的服务器证书校验）
      REQUIRES esp_wifi esp_netif esp_event esp_http_client mbedtls
)
