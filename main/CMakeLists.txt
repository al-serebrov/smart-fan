idf_component_register(SRCS "main.c" "fsm.c"
                    INCLUDE_DIRS "."
                    REQUIRES aht ssd1306 esp_timer)
