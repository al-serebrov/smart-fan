idf_component_register(SRCS "time_sync_wifi.c" "main.c" "fsm.c"
                    INCLUDE_DIRS "."
                    REQUIRES aht ssd1306 esp_timer u8g2 u8g2-hal-esp-idf nvs_flash esp_wifi)