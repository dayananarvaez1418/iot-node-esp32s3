idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
PRIV_REQUIRES driver esp_driver_i2c esp_wifi esp_event
              esp_netif nvs_flash esp_https_ota esp_http_client
              app_update esp-tls esp_timer