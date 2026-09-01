#ifndef DHT22_H
#define DHT22_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t pin;
    float temperature;
    float humidity;
} dht22_config_t;

/**
 * @brief Initialize GPIO pin for DHT22 sensor module.
 */
esp_err_t dht22_init(gpio_num_t pin);

/**
 * @brief Read temperature and relative humidity from DHT22 sensor.
 * 
 * @param pin GPIO pin number connected to DHT22 data line
 * @param temperature Pointer to output float for temperature in Celsius
 * @param humidity Pointer to output float for relative humidity in %
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT or ESP_ERR_INVALID_CRC on failure
 */
esp_err_t dht22_read_data(gpio_num_t pin, float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif // DHT22_H
