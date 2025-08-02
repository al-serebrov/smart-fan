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

#define I2C_MASTER_PORT I2C_NUM_0
#define I2C_MASTER_SDA GPIO_NUM_5
#define I2C_MASTER_SCL GPIO_NUM_6

#define RELAY_GPIO GPIO_NUM_3
#define BUTTON_GPIO GPIO_NUM_7

#define RELAY_ON 0
#define RELAY_OFF 1
#define DEBOUNCE_DELAY_MS 50

SSD1306_t dev = {
    ._address = 0x3C,
    ._width = 128,
    ._height = 64,
    ._pages = 8,
    ._flip = false};

volatile bool relay_state = false;

void my_i2c_master_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0));
}

void check_button_task(void *arg)
{
    bool last_state = true; // Кнопка у HIGH (через pull-up)
    TickType_t last_debounce_time = 0;

    while (1)
    {
        bool current_state = gpio_get_level(BUTTON_GPIO);

        if (current_state != last_state)
        {
            last_debounce_time = xTaskGetTickCount();
            last_state = current_state;
        }

        if (!current_state && // натиснуто (LOW)
            (xTaskGetTickCount() - last_debounce_time) > pdMS_TO_TICKS(DEBOUNCE_DELAY_MS))
        {
            // Чекаємо поки відпустять кнопку
            while (gpio_get_level(BUTTON_GPIO) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // Обробка кліку
            relay_state = !fsm_is_fan_on(); 
            fsm_set_manual_override(relay_state);
            printf("Relay toggled: %s\n", fsm_is_fan_on() ? "ON" : "OFF");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    printf("Booting...\n");

    // Реле
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Кнопка
    gpio_config_t btn_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_conf);
    // Запускаємо debounce таску
    xTaskCreate(check_button_task, "check_button_task", 2048, NULL, 10, NULL);

    // Один спільний I2C на OLED + AHT10
    my_i2c_master_init(I2C_MASTER_PORT, I2C_MASTER_SDA, I2C_MASTER_SCL);

    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);
    ssd1306_display_text(&dev, 4, "Loading...", strlen("Loading..."), true);
    ssd1306_clear_screen(&dev, false);

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

    int timer_strlen = 0;
    while (1)
    {
        esp_err_t err = aht_read(&temp, &hum);
        if (err == ESP_OK)
        {
            fsm_update(hum);
            char line[32];
            snprintf(line, sizeof(line), "T: %.1fC H: %.1f%%\n", temp, hum);
            // printf(line, sizeof(line), "T: %.1fC H: %.1f%%\n", temp, hum);

            char temp_line[32];
            char hum_line[32];
            char fan_line[20];
            char timer_line[20];
            char state_line[20];
            fsm_get_display_lines(fan_line, timer_line, state_line);


            snprintf(temp_line, sizeof(temp_line), "T: %.1fC", temp);
            snprintf(hum_line, sizeof(hum_line), "H: %.1f%%", hum); // %% escapes the percent sign

            // No need to redraw
            ssd1306_display_text(&dev, 3, temp_line, strlen(temp_line), false);
            ssd1306_display_text(&dev, 4, hum_line, strlen(hum_line), false);
            ssd1306_display_text(&dev, 5, fan_line, strlen(fan_line), false);
            ssd1306_display_text(&dev, 7, state_line, strlen(state_line), false);

            if (strlen(timer_line) != timer_strlen) {
                // Timer: clear if needed
                ssd1306_clear_line(&dev, 6, false);
                timer_strlen = strlen(timer_line);
            }
            ssd1306_display_text(&dev, 6, timer_line, strlen(timer_line), false);

            // 3. Show all at once — no flicker!
            ssd1306_show_buffer(&dev);
            // ssd1306_clear_screen(&dev, false);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}