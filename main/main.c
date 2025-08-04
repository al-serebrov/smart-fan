#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "aht.h"
#include "ssd1306.h"
#include "esp_mac.h"
#include <string.h>
#include "fsm.h"
#include "u8g2.h"
#include "u8x8.h"
#include "u8g2_esp32_hal.h"
#include "nvs_flash.h"
#include "nvs.h"

// ----- Display setup -----
u8g2_t u8g2;

#define DISPLAY_OFFSET_X 28
#define DISPLAY_OFFSET_Y 24
#define OFFSET_X(x) ((x) + DISPLAY_OFFSET_X)
#define OFFSET_Y(y) ((y) + DISPLAY_OFFSET_Y)

#define UI_PAGE_STATUS 0
#define UI_PAGE_LOGS   1
#define UI_PAGE_COUNT  2
#define UI_SCREEN_INTERVAL_TICKS 15  // 15 seconds
#define LOG_SCROLL_INTERVAL_TICKS 1  // scroll logs every second
#define MAX_LOG_LINES 5
#define MAX_LOG_ENTRIES 50

static int ui_screen_index = 0;
static int log_scroll_tick = 0;
static int log_scroll_offset = 0;

// ----- I2C Setup -----
#define I2C_MASTER_PORT I2C_NUM_0
#define I2C_MASTER_SDA GPIO_NUM_5
#define I2C_MASTER_SCL GPIO_NUM_6

// --- Relay and button ---
#define RELAY_GPIO GPIO_NUM_3
#define BUTTON_GPIO GPIO_NUM_7

#define RELAY_ON 0
#define RELAY_OFF 1
#define DEBOUNCE_DELAY_MS 50

volatile bool relay_state = false;

void my_i2c_master_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
{
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = I2C_MASTER_SDA;
    u8g2_esp32_hal.bus.i2c.scl = I2C_MASTER_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
}

void check_button_task(void *arg)
{
    bool last_state = true; // Button is is in the HIGH (through pull-up)
    TickType_t last_debounce_time = 0;

    while (1)
    {
        bool current_state = gpio_get_level(BUTTON_GPIO);

        if (current_state != last_state)
        {
            last_debounce_time = xTaskGetTickCount();
            last_state = current_state;
        }

        if (!current_state && // pressed (LOW)
            (xTaskGetTickCount() - last_debounce_time) > pdMS_TO_TICKS(DEBOUNCE_DELAY_MS))
        {
            // Wait for the button release
            while (gpio_get_level(BUTTON_GPIO) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // Click handler
            relay_state = !fsm_is_fan_on();
            fsm_set_manual_override(relay_state);
            printf("Relay toggled: %s\n", fsm_is_fan_on() ? "ON" : "OFF");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void draw_log_screen()
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_profont10_tr);

    nvs_handle_t handle;
    if (nvs_open("fsm_log", NVS_READONLY, &handle) != ESP_OK)
    {
        u8g2_DrawStr(&u8g2, OFFSET_X(0), OFFSET_Y(10), "Log: NVS error");
        u8g2_SendBuffer(&u8g2);
        return;
    }
    
    char lines[MAX_LOG_LINES][64];
    int shown = 0;

    // Go from newest to oldest
    for (int i = MAX_LOG_ENTRIES - 1 - log_scroll_offset;
         i >= 0 && shown < MAX_LOG_LINES;
         i--)
    {
        char key[32];
        size_t len = sizeof(lines[shown]);
        snprintf(key, sizeof(key), "entry_%02d", i);

        if (nvs_get_str(handle, key, lines[shown], &len) == ESP_OK)
        {
            shown++;
        }
    }

    nvs_close(handle);
    
    // Draw from 0 → shown-1 → newest at top
    for (int i = 0; i < shown; i++)
    {
        int y = OFFSET_Y(8 + i * 8);  // line height = 8px
        u8g2_DrawStr(&u8g2, OFFSET_X(0), y, lines[i]);
    }

    u8g2_SendBuffer(&u8g2);
}

void draw_current_state(const char *hum_line, const char *temp_line)
{
    static const uint8_t image_choice_bullet_off_bits[] = {0xe0, 0x03, 0x38, 0x0e, 0x0c, 0x18, 0x06, 0x30, 0x02, 0x20, 0x03, 0x60, 0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x03, 0x60, 0x02, 0x20, 0x06, 0x30, 0x0c, 0x18, 0x38, 0x0e, 0xe0, 0x03, 0x00, 0x00};
    static const uint8_t image_choice_bullet_on_bits[] = {0xe0, 0x03, 0x38, 0x0e, 0xcc, 0x19, 0xf6, 0x37, 0xfa, 0x2f, 0xfb, 0x6f, 0xfd, 0x5f, 0xfd, 0x5f, 0xfd, 0x5f, 0xfb, 0x6f, 0xfa, 0x2f, 0xf6, 0x37, 0xcc, 0x19, 0x38, 0x0e, 0xe0, 0x03, 0x00, 0x00};
    static const uint8_t image_Hum_arrow_bits[] = {0x80, 0xff, 0xff, 0x7f, 0x40, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
    static const uint8_t image_Temp_arrow_bits[] = {0xff, 0xff, 0x7f};
    static const uint8_t image_weather_humidity_white_bits[] = {0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x20, 0x00, 0x00, 0x30, 0x00, 0x00, 0x50, 0x00, 0x00, 0x48, 0x00, 0x00, 0x88, 0x00, 0x00, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x82, 0x02, 0x00, 0x02, 0x03, 0x00, 0x01, 0x05, 0x00, 0x01, 0x04, 0x00, 0x02, 0x02, 0x00, 0x02, 0x02, 0x00, 0x0c, 0x01, 0x00, 0xf0, 0x00, 0x00};
    static const uint8_t image_weather_temperature_bits[] = {0x38, 0x00, 0x44, 0x40, 0xd4, 0xa0, 0x54, 0x40, 0xd4, 0x1c, 0x54, 0x06, 0xd4, 0x02, 0x54, 0x02, 0x54, 0x06, 0x92, 0x1c, 0x39, 0x01, 0x75, 0x01, 0x7d, 0x01, 0x39, 0x01, 0x82, 0x00, 0x7c, 0x00};
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetBitmapMode(&u8g2, 1);
    u8g2_SetFontMode(&u8g2, 1);

    // Humidity
    u8g2_SetFont(&u8g2, u8g2_font_profont11_tr);
    u8g2_DrawStr(&u8g2, OFFSET_X(19), OFFSET_Y(8), hum_line);

    // Temp arrow
    u8g2_DrawXBM(&u8g2, OFFSET_X(19), OFFSET_Y(9), 23, 1, image_Temp_arrow_bits);

    // weather_temperature
    u8g2_DrawXBM(&u8g2, OFFSET_X(0), OFFSET_Y(0), 16, 16, image_weather_temperature_bits);

    // weather_humidity_white
    u8g2_DrawXBM(&u8g2, OFFSET_X(0), OFFSET_Y(9), 19, 27, image_weather_humidity_white_bits);

    // Hum arrow
    u8g2_DrawXBM(&u8g2, OFFSET_X(11), OFFSET_Y(29), 31, 7, image_Hum_arrow_bits);

    // Layer 5
    u8g2_DrawStr(&u8g2, OFFSET_X(18), OFFSET_Y(27), temp_line);

    const uint8_t *state_icon = fsm_get_state_icon();
    u8g2_DrawXBM(&u8g2, OFFSET_X(56), OFFSET_Y(16), 15, 16, state_icon);

    // Layer 11
    u8g2_SetFont(&u8g2, u8g2_font_profont10_tr);
    char fan_line[20];
    char timer_line[20];
    char state_line[20];
    fsm_get_display_lines(fan_line, timer_line, state_line);

    u8g2_DrawStr(&u8g2, OFFSET_X(42), OFFSET_Y(39), timer_line);

    if (!fsm_is_fan_on())
    {
        // choice_bullet_off
        u8g2_DrawXBM(&u8g2, OFFSET_X(57), OFFSET_Y(0), 15, 16, image_choice_bullet_off_bits);
    }
    else
    {
        // choice_bullet_on
        u8g2_DrawXBM(&u8g2, OFFSET_X(57), OFFSET_Y(0), 15, 16, image_choice_bullet_on_bits);
    }

    u8g2_SendBuffer(&u8g2);
}

void app_main(void)
{
    printf("Booting...\n");

    // Relay
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Button
    gpio_config_t btn_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_conf);
    // Run the debounce task
    xTaskCreate(check_button_task, "check_button_task", 2048, NULL, 10, NULL);

    // One single I2C for OLED + AHT10
    my_i2c_master_init(I2C_MASTER_PORT, I2C_MASTER_SDA, I2C_MASTER_SCL);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_profont11_tr);
    u8g2_DrawStr(&u8g2, DISPLAY_OFFSET_X + 0, 32, "Loading...");
    u8g2_SendBuffer(&u8g2);

    aht_init(I2C_MASTER_PORT);
    vTaskDelay(pdMS_TO_TICKS(200));
    fsm_init();

    float temp = 0.0, hum = 0.0;
    printf("Scanning I2C bus...\n");
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        uint8_t dummy = 0;
        esp_err_t ret = i2c_master_write_to_device(I2C_MASTER_PORT, addr, &dummy, 1, pdMS_TO_TICKS(10));
        if (ret == ESP_OK)
        {
            printf("Found I2C device at 0x%02X\n", addr);
        }
    }

    int tick_count = 0;
    while (1)
    {
        esp_err_t err = aht_read(&temp, &hum);
        if (err == ESP_OK)
        {
            // char line[32];
            // snprintf(line, sizeof(line), "T: %.1fC H: %.1f%%\n", temp, hum);
            // printf(line); // logging to the console

            char temp_line[32];
            char hum_line[32];
            snprintf(temp_line, sizeof(temp_line), "%.1f", temp);
            snprintf(hum_line, sizeof(hum_line), "%.1f", hum);

            fsm_update(hum);
            
            // UI page switch every 15 seconds
            tick_count++;
            if (tick_count >= UI_SCREEN_INTERVAL_TICKS)
            {
                ui_screen_index = (ui_screen_index + 1) % UI_PAGE_COUNT;
                tick_count = 0;
                log_scroll_offset = 0;
            }

            // Scroll logs when in log view
            if (ui_screen_index == UI_PAGE_LOGS)
            {
                log_scroll_tick++;
                if (log_scroll_tick >= LOG_SCROLL_INTERVAL_TICKS)
                {
                    log_scroll_tick = 0;
                    log_scroll_offset++;
                    if (log_scroll_offset > MAX_LOG_ENTRIES - MAX_LOG_LINES)
                        log_scroll_offset = 0;
                }
                draw_log_screen(log_scroll_offset);
            }
            else
            {
                draw_current_state(temp_line, hum_line);
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}