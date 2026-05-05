#pragma once
#include "driver/gpio.h"


#define DHT_OK      0
#define DHT_ERROR  -1

// 直接在 Wokwi 上可運作的純 C 實作（不需要額外 component）
esp_err_t dht_read_data(gpio_num_t gpio_num, float *temperature, float *humidity);