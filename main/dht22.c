#include "dht22.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "DHT22_DRIVER";

esp_err_t dht22_init(gpio_num_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err == ESP_OK) {
        gpio_set_level(pin, 1);
    }
    return err;
}

static inline int wait_for_level(gpio_num_t pin, int level, uint32_t timeout_us)
{
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if ((esp_timer_get_time() - start_time) > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start_time);
}

esp_err_t dht22_read_data(gpio_num_t pin, float *temperature, float *humidity)
{
    if (!temperature || !humidity) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[5] = {0};

    // Enter critical section to prevent FreeRTOS task preemption during sub-millisecond bit timing
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    // Phase 1: Send Start Signal (Pull low for 20ms for DHT22/AM2302 spec)
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    ets_delay_us(20000); // 20ms low pulse required for DHT22 wake-up
    gpio_set_level(pin, 1);
    ets_delay_us(30);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    // Phase 2: Sensor Response Handshake (80us Low, 80us High)
    // 1000us timeout accommodates Wokwi simulation event loop timing
    if (wait_for_level(pin, 0, 1000) < 0 || wait_for_level(pin, 1, 1000) < 0 || wait_for_level(pin, 0, 1000) < 0) {
        portEXIT_CRITICAL(&mux);
        ESP_LOGE(TAG, "DHT22 Hardware Handshake Timeout on GPIO %d", pin);
        return ESP_ERR_TIMEOUT;
    }

    // Phase 3: Read 40 Bits (5 Bytes)
    for (int i = 0; i < 40; i++) {
        if (wait_for_level(pin, 1, 1000) < 0) {
            portEXIT_CRITICAL(&mux);
            ESP_LOGE(TAG, "DHT22 Bit %d High Timeout", i);
            return ESP_ERR_TIMEOUT;
        }

        int high_duration = wait_for_level(pin, 0, 1000);
        if (high_duration < 0) {
            portEXIT_CRITICAL(&mux);
            ESP_LOGE(TAG, "DHT22 Bit %d Low Timeout", i);
            return ESP_ERR_TIMEOUT;
        }

        // Logic 0 is ~26-28us pulse, Logic 1 is ~70us pulse
        if (high_duration > 40) {
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    portEXIT_CRITICAL(&mux);

    // Phase 4: Verify Checksum
    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (data[4] != checksum) {
        ESP_LOGE(TAG, "DHT22 CRC mismatch: calc=0x%02X, rx=0x%02X", checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    // Phase 5: Convert Raw Bytes to Engineering Units
    uint16_t raw_humidity = (data[0] << 8) | data[1];
    int16_t raw_temperature = ((data[2] & 0x7F) << 8) | data[3];
    if (data[2] & 0x80) {
        raw_temperature = -raw_temperature;
    }

    *humidity = raw_humidity / 10.0f;
    *temperature = raw_temperature / 10.0f;

    ESP_LOGI(TAG, "DHT22 Read Success: Temp=%.1f C, Humidity=%.1f %%", *temperature, *humidity);
    return ESP_OK;
}
