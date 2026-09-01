#ifndef SENSOR_ENGINE_H
#define SENSOR_ENGINE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_GPIO_PIN        GPIO_NUM_5
#define SENSOR_SAMPLE_INTERVAL_MS 5000

/**
 * @brief Initialize sensor engine hardware and configuration.
 */
esp_err_t sensor_engine_init(gpio_num_t pin);

/**
 * @brief FreeRTOS task body for periodic sensor sampling.
 */
void sensor_engine_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_ENGINE_H
