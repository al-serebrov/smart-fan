#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

esp_err_t aht_init(i2c_port_t port);
esp_err_t aht_read(float *temperature, float *humidity);