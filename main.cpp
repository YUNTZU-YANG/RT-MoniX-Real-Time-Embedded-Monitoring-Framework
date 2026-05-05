#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_random.h" // 用於 DHT 模擬數據

// 引入 ADC 驅動程式
#include "esp_adc/adc_oneshot.h" 
#include "driver/gpio.h" // 用於 DHT GPIO 操作

// =========================================================================
// I. 巨集與常數定義
// =========================================================================
#define TAG "MULTI_SENSOR"

// 感測器 GPIO 定義
#define DHT_GPIO_PIN GPIO_NUM_4          // DHT22 連接到 GPIO 4
#define ADC_UNIT ADC_UNIT_1              // LDR 連接到 ADC1
#define ADC_CHANNEL ADC_CHANNEL_0        // LDR 連接到 VP (GPIO 36)
#define ADC_ATTEN ADC_ATTEN_DB_12        // ADC 衰減設置

// DHT 模擬回傳值 (真實驅動庫的成功碼通常為 0)
#define DHT_SUCCESS 0 

// 任務句柄 (用於 Task Notification)
static TaskHandle_t xLDRTaskHandle = NULL;
static TaskHandle_t xDHTTaskHandle = NULL;

// 數據結構 (將 LDR 和 DHT 數據同步儲存)
typedef struct {
    float temperature;
    float humidity;
    int adc_value;
} CombinedData_t;

// 數據隊列 (用於將同步數據傳遞給未來的顯示任務等)
static QueueHandle_t xCombinedDataQueue;


// =========================================================================
// II. 模擬 DHT 驅動程式
// =========================================================================

// 目的：避免連結錯誤，直到您加入真實的 DHT 驅動庫。
// 作用：模擬讀取 DHT 數據並返回成功。
int dht_read_data(gpio_num_t gpio_num, float *temperature, float *humidity)
{
    // 模擬讀數
    *temperature = 25.0f + ((float)(esp_random() % 100)) / 100.0f; // 25.00C - 25.99C
    *humidity = 60.0f + ((float)(esp_random() % 50)) / 100.0f;      // 60.00% - 60.49%
    // 模擬真實 DHT 讀取所需的延遲 (約 250ms)
    vTaskDelay(pdMS_TO_TICKS(250)); 
    return DHT_SUCCESS; 
}


// =========================================================================
// III. 感測器任務
// =========================================================================

// 1. LDR 讀取任務
static void LDRReadTask(void *pvParameter)
{
    // ADC 初始化程式碼 (與之前相同)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
    adc_oneshot_chan_cfg_t config = { .atten = ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    int adc_raw = 0;
    
    for (;;) {
        // 目的：等待協調者任務的通知 (計時器訊號)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // 清除通知並等待

        // 讀取 ADC 原始值
        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw) != ESP_OK) {
             ESP_LOGE(TAG, "LDR Read Failed!");
             printf("LDR Read Failed!\n") ; 
        } else {
             ESP_LOGI(TAG, "LDR Read Success: %d", adc_raw);
             printf("LDR Read Success: %d\n", adc_raw);
        }

        // 將 ADC 值傳送給 DHT 任務進行同步報告
        xTaskNotify(xDHTTaskHandle, (uint32_t)adc_raw, eSetValueWithOverwrite);
    }
}

// 2. DHT 讀取任務
static void DHTReadTask(void *pvParameter)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint32_t ulNotifiedValue;
    CombinedData_t combined_data;

    for (;;) {
        // 目的：等待協調者任務的通知 (計時器訊號)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // 清除通知並等待

        // 讀取 DHT 數據 (使用模擬函數)
        if (dht_read_data(DHT_GPIO_PIN, &temperature, &humidity) != DHT_SUCCESS) {
            ESP_LOGE(TAG, "DHT Read Failed!");
            printf("DHT Read Failed!\n");
        } else {
            ESP_LOGI(TAG, "DHT Read Success: Temp=%.1fC, Humid=%.1f%%", temperature, humidity);
            printf("DHT Read Success: Temp=%.1fC, Humid=%.1f%%\n", temperature, humidity);
        }

        // 等待 LDR 任務傳來的 ADC 數據
        // 目的：確保兩個感測器都在同一週期內讀取完成
        if (xTaskNotifyWait(0x00, ULONG_MAX, &ulNotifiedValue, pdMS_TO_TICKS(500)) == pdPASS) {
            combined_data.adc_value = (int)ulNotifiedValue;
            combined_data.temperature = temperature;
            combined_data.humidity = humidity;

            // 報告同步結果
            ESP_LOGW(TAG, "--- SYNCHRONIZED DATA ---");
            ESP_LOGW(TAG, "T:%.1fC, H:%.1f%%, LDR:%d", combined_data.temperature, combined_data.humidity, combined_data.adc_value);
            ESP_LOGW(TAG, "-------------------------");


            printf("--- SYNCHRONIZED DATA ---\n");
            printf("T:%.1fC, H:%.1f%%, LDR:%d\n", combined_data.temperature, combined_data.humidity, combined_data.adc_value);
            printf( "-------------------------\n");

            // 將同步數據發送給隊列 (供未來顯示任務使用)
            xQueueSend(xCombinedDataQueue, &combined_data, 0);

        } else {
            // LDR 任務可能超時或失敗
            ESP_LOGE(TAG, "LDR data synchronization failed/timeout!");
            printf("LDR data synchronization failed/timeout!\n");
        }
    }
}

// 3. 計時器協調者任務 (同步控制核心)
static void TimerCoordinatorTask(void *pvParameter)
{
    const TickType_t xFrequency = pdMS_TO_TICKS(5000); // 每 5 秒同步一次
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // 目的：精確地每 5 秒喚醒所有感測器任務
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        // 發送通知給 LDR 任務，開始讀取
        xTaskNotifyGive(xLDRTaskHandle); 
        // 發送通知給 DHT 任務，開始讀取
        xTaskNotifyGive(xDHTTaskHandle);

        ESP_LOGI(TAG, "Coordinator signal sent. Starting a new 5-second sensor cycle.");
        printf( "Coordinator signal sent. Starting a new 5-second sensor cycle.\n");
    }
}


// =========================================================================
// IV. 主入口點 (app_main)
// =========================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "Multi-Sensor Synchronization Application Start.");

    // 創建數據隊列，用於保存同步後的結果
    xCombinedDataQueue = xQueueCreate(1, sizeof(CombinedData_t));

    // 創建感測器任務 (獲取任務句柄，用於通知)
    // 注意：將任務句柄賦予給靜態變數 xLDRTaskHandle/xDHTTaskHandle
    xTaskCreate(LDRReadTask, "LDRReadTask", 3072, NULL, 5, &xLDRTaskHandle);
    xTaskCreate(DHTReadTask, "DHTReadTask", 3072, NULL, 5, &xDHTTaskHandle);

    // 創建最高優先級的協調者任務
    xTaskCreate(TimerCoordinatorTask, "CoordinatorTask", 2048, NULL, 10, NULL);

    ESP_LOGI(TAG, "Tasks and Coordinator created. Synchronization started.");
    printf("Tasks and Coordinator created. Synchronization started.\n");
}