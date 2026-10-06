#include "valve_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "VALVE_CONTROL";

static gpio_num_t s_valve_pin = DEFAULT_VALVE_GPIO_PIN;
static float s_pressure_threshold = DEFAULT_PRESSURE_THRESHOLD;
static bool s_valve_state = false;
static SemaphoreHandle_t s_threshold_mutex = NULL;

esp_err_t valve_control_init(gpio_num_t pin, float initial_threshold)
{
    s_valve_pin = pin;
    s_pressure_threshold = initial_threshold;
    s_valve_state = false;

    s_threshold_mutex = xSemaphoreCreateMutex();
    if (!s_threshold_mutex) {
        ESP_LOGE(TAG, "Failed to create threshold mutex");
        return ESP_FAIL;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_valve_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d for valve control", s_valve_pin);
        return err;
    }

    // Default pin state LOW (closed)
    gpio_set_level(s_valve_pin, 0);

    ESP_LOGI(TAG, "Valve Control Initialized [GPIO: %d, Initial Threshold: %.2f]",
             s_valve_pin, s_pressure_threshold);
    return ESP_OK;
}

void valve_control_set_threshold(float threshold)
{
    if (s_threshold_mutex && xSemaphoreTake(s_threshold_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_pressure_threshold = threshold;
        xSemaphoreGive(s_threshold_mutex);
        ESP_LOGI(TAG, "Pressure threshold updated to: %.2f", threshold);
    } else {
        s_pressure_threshold = threshold; // Fallback direct assignment
    }
}

float valve_control_get_threshold(void)
{
    float val = DEFAULT_PRESSURE_THRESHOLD;
    if (s_threshold_mutex && xSemaphoreTake(s_threshold_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        val = s_pressure_threshold;
        xSemaphoreGive(s_threshold_mutex);
    } else {
        val = s_pressure_threshold;
    }
    return val;
}

bool valve_control_get_state(void)
{
    return s_valve_state;
}

bool valve_control_evaluate(float pressure)
{
    float thresh = valve_control_get_threshold();
    bool new_state = (pressure >= thresh);

    if (new_state != s_valve_state) {
        ESP_LOGI(TAG, "🚨 VALVE STATE CHANGE: Pressure %.2f %s Threshold %.2f -> Valve %s (GPIO %d)",
                 pressure, new_state ? ">=" : "<", thresh,
                 new_state ? "TRIGGERED (HIGH)" : "CLOSED (LOW)", s_valve_pin);
    }

    s_valve_state = new_state;
    gpio_set_level(s_valve_pin, new_state ? 1 : 0);
    return s_valve_state;
}
