#include "system_health.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "http_telemetry.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYSTEM_HEALTH";

esp_err_t system_health_init(uint32_t timeout_seconds)
{
    ESP_LOGI(TAG, "Initializing Task Watchdog Timer (TWDT) with %lu s timeout...", timeout_seconds);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = timeout_seconds * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Monitor idle tasks on both cores
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_init(&twdt_config);
    if (err == ESP_ERR_INVALID_STATE) {
        // TWDT pre-initialized at startup in ESP-IDF v5.x; reconfigure configuration parameters
        ESP_LOGI(TAG, "TWDT pre-initialized by system startup. Reconfiguring timeout to %lu s...", timeout_seconds);
        err = esp_task_wdt_reconfigure(&twdt_config);
    }
    return err;
#else
    esp_err_t err = esp_task_wdt_init(timeout_seconds, true);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    return err;
#endif
}

void system_health_task(void *pvParameters)
{
    ESP_LOGI(TAG, "System Health Monitor Task starting on Core %d", xPortGetCoreID());
    esp_task_wdt_add(NULL);

    while (1) {
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_heap = esp_get_minimum_free_heap_size();
        uint32_t pending_offline = storage_get_pending_count();
        bool wifi_online = wifi_manager_is_connected();
        bool http_online = http_telemetry_is_connected();

        ESP_LOGI(TAG, "------------------- SYSTEM DIAGNOSTICS METRICS -------------------");
        ESP_LOGI(TAG, "| Free Heap: %lu Bytes | Min Free Ever: %lu Bytes", free_heap, min_heap);
        ESP_LOGI(TAG, "| Network Link: %s | HTTP Endpoint: %s",
                 wifi_online ? "ONLINE" : "DISCONNECTED",
                 http_online ? "READY" : "DISCONNECTED");
        ESP_LOGI(TAG, "| Offline Store-and-Forward Pending Backlog: %lu Records", pending_offline);
        ESP_LOGI(TAG, "-------------------------------------------------------------------");

        if (min_heap < 20480) { // Warning under 20KB free heap
            ESP_LOGW(TAG, "HEAP MEMORY ALARM: Low memory threshold detected (< 20KB free)");
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Sample health status every 10 seconds
    }
}
