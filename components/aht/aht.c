#include "aht.h"
#include "driver/i2c.h"
#include "freertos/task.h"
#include "esp_mac.h"

#define AHT10_ADDRESS 0x38
static i2c_port_t aht_port;

esp_err_t aht_init(i2c_port_t port) {
    aht_port = port;
    uint8_t cmd[] = {0xBE, 0x08, 0x00};
    return i2c_master_write_to_device(port, AHT10_ADDRESS, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
}

esp_err_t aht_read(float *temperature, float *humidity) {
    uint8_t data[6];
    
    uint8_t trigger_cmd[] = {0xAC, 0x33, 0x00};
    i2c_master_write_to_device(aht_port, 0x38, trigger_cmd, sizeof(trigger_cmd), pdMS_TO_TICKS(100));

    vTaskDelay(pdMS_TO_TICKS(80));

    esp_err_t err = i2c_master_read_from_device(aht_port, AHT10_ADDRESS, data, 6, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;

    uint32_t raw_hum = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *humidity = ((float)raw_hum) * 100 / 1048576;
    *temperature = ((float)raw_temp) * 200 / 1048576 - 50;
    return ESP_OK;
}
