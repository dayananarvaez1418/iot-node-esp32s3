idf_component_register(
    SRCS "main.c" "ota_manager.c" "net_edge.c" "net_espnow.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_driver_i2c esp_driver_gpio esp_wifi esp_event
                  esp_netif nvs_flash esp_https_ota esp_http_client
                  app_update esp-tls esp_timer esp_hw_support
    EMBED_TXTFILES "server_certs/ca_cert.pem"
)