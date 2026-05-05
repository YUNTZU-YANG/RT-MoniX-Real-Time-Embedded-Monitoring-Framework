#include "dht.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

esp_err_t dht_read_data(gpio_num_t gpio_num, float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    uint64_t timeout = esp_timer_get_time() + 300000;  // 300ms 總超時

    // ===== 1. 發送起始訊號（Wokwi 專用加強版）=====
    gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num, 0);
    vTaskDelay(pdMS_TO_TICKS(25));           // 必須 ≥ 20ms，25ms 最穩
    gpio_set_level(gpio_num, 1);
    
    // 關鍵！Wokwi 必須拉高至少 40µs 以上才認得
    ets_delay_us(50);                        // 50µs 拉高（原版 30µs 太短）
    
    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);

    // ===== 2. 等待 DHT22 回應（80µs low + 80µs high）=====
    while (gpio_get_level(gpio_num) == 1)
        if (esp_timer_get_time() > timeout) return DHT_ERROR;
    while (gpio_get_level(gpio_num) == 0)
        if (esp_timer_get_time() > timeout) return DHT_ERROR;
    while (gpio_get_level(gpio_num) == 1)
        if (esp_timer_get_time() > timeout) return DHT_ERROR;

    // ===== 3. 讀取 40 bits =====
    for (int i = 0; i < 40; i++) {
        // 等待 low 結束
        while (gpio_get_level(gpio_num) == 0)
            if (esp_timer_get_time() > timeout) return DHT_ERROR;

        uint32_t high_time = 0;
        while (gpio_get_level(gpio_num) == 1) {
            ets_delay_us(1);
            high_time++;
            if (high_time > 200) return DHT_ERROR;
        }

        if (high_time > 45) {  // 50µs → 1, 28µs → 0（Wokwi 實際測出來 45 最穩）
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    // ===== 4. 檢查 checksum =====
    if (data[4] != (data[0] + data[1] + data[2] + data[3]))
        return DHT_ERROR;

    // ===== 5. 解析資料 =====
    *humidity = ((data[0] << 8) | data[1]) / 10.0f;
    int16_t t = ((data[2] & 0x7F) << 8) | data[3];
    *temperature = t / 10.0f;
    if (data[2] & 0x80) *temperature = -(*temperature);

    return DHT_OK;
}