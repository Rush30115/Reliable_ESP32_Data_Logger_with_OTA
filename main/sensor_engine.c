#include "sensor_engine.h"
#include "sensor_record.h"
#include "dht22.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

static const char *TAG = "SENSOR_ENGINE";
extern QueueHandle_t xSensorQueue;
static gpio_num_t s_sensor_pin = SENSOR_GPIO_PIN;

esp_err_t sensor_engine_init(gpio_num_t pin)
{
    s_sensor_pin = pin;
    return dht22_init(pin);
}

void sensor_engine_task(void *pvParameters)
{
    sensor_record_t record;
    uint32_t record_counter = 0;

    ESP_LOGI(TAG, "Sensor Engine Task starting on Core %d", xPortGetCoreID());

    // Register this task with Task Watchdog Timer
    esp_task_wdt_add(NULL);

    while (1) {
        float temp = 0.0f;
        float hum = 0.0f;

        esp_err_t ret = dht22_read_data(s_sensor_pin, &temp, &hum);

        record.timestamp_us = esp_timer_get_time();
        record.record_id = ++record_counter;
        record.temperature = temp;
        record.humidity = hum;
        record.pressure = 1013.25f; // Standard atmosphere hPa
        record.battery_mv = 3300;   // Nominal 3.3V supply
        record.sensor_status = (ret == ESP_OK) ? 0x01 : 0x02;
        record.reserved = 0x00;
        record.crc16 = sensor_record_calc_crc(&record);

        if (xSensorQueue != NULL) {
            if (xQueueSendToBack(xSensorQueue, &record, pdMS_TO_TICKS(200)) == pdTRUE) {
                ESP_LOGI(TAG, "Record #%lu enqueued [Temp=%.1fC, Hum=%.1f%%, CRC=0x%04X]",
                         record.record_id, record.temperature, record.humidity, record.crc16);
            } else {
                ESP_LOGW(TAG, "xSensorQueue FULL! Dropped Record #%lu", record.record_id);
            }
        }

        // Feed watchdog timer
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS));
    }
}
