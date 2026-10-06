#include "sensor_engine.h"
#include "sensor_record.h"
#include "dht22.h"
#include "sht40.h"
#include "valve_control.h"
#include "oled_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

static const char *TAG = "SENSOR_ENGINE";
extern QueueHandle_t xSensorQueue;
static gpio_num_t s_sensor_pin = SENSOR_GPIO_PIN;
static float s_last_pressure = 0.0f;

esp_err_t sensor_engine_init(gpio_num_t pin)
{
    s_sensor_pin = pin;
    // Probe 7Semi SHT40 on primary I2C bus (I2C_NUM_0, 0x44)
    sht40_init(I2C_NUM_0);
    return dht22_init(pin);
}

float sensor_engine_get_last_pressure(void)
{
    return s_last_pressure;
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

        // Attempt read from 7Semi SHT40 I2C probe first (I2C Address 0x44)
        esp_err_t ret = sht40_read(I2C_NUM_0, &temp, &hum);
        if (ret != ESP_OK) {
            // Fallback to DHT22 single-wire digital read
            ret = dht22_read_data(s_sensor_pin, &temp, &hum);
        }

        // Treat temperature sensor input as simulated pressure reading
        float pressure_reading = (ret == ESP_OK) ? temp : 0.0f;
        s_last_pressure = pressure_reading;

        // Evaluate valve control pin against current dynamic threshold
        bool valve_active = valve_control_evaluate(pressure_reading);

        record.timestamp_us = esp_timer_get_time();
        record.record_id = ++record_counter;
        record.temperature = temp;
        record.humidity = hum;
        record.pressure = pressure_reading; // Pressure stand-in value
        record.battery_mv = 3300;   // Nominal 3.3V supply
        record.sensor_status = (ret == ESP_OK) ? 0x01 : 0x02;
        record.reserved = 0x00;
        record.crc16 = sensor_record_calc_crc(&record);

        // Update OLED Display with live temperature, humidity, pressure & valve status
        oled_display_show_sensor_data(temp, hum, pressure_reading, valve_active);

        if (xSensorQueue != NULL) {
            if (xQueueSendToBack(xSensorQueue, &record, pdMS_TO_TICKS(200)) == pdTRUE) {
                ESP_LOGI(TAG, "Record #%lu enqueued [Pressure=%.1f (Sim), Hum=%.1f%%, Valve=%s, CRC=0x%04X]",
                         record.record_id, record.pressure, record.humidity,
                         valve_active ? "ON" : "OFF", record.crc16);
            } else {
                ESP_LOGW(TAG, "xSensorQueue FULL! Dropped Record #%lu", record.record_id);
            }
        }

        // Feed watchdog timer
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS));
    }
}

