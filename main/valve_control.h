#ifndef VALVE_CONTROL_H
#define VALVE_CONTROL_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_VALVE_GPIO_PIN      GPIO_NUM_12
#define DEFAULT_PRESSURE_THRESHOLD 30.0f

/**
 * @brief Initialize valve control GPIO output pin and default pressure threshold.
 * 
 * @param pin GPIO pin for relay/LED output
 * @param initial_threshold Default threshold value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t valve_control_init(gpio_num_t pin, float initial_threshold);

/**
 * @brief Thread-safe update of the pressure threshold value at runtime.
 * 
 * @param threshold New threshold value
 */
void valve_control_set_threshold(float threshold);

/**
 * @brief Thread-safe retrieval of the current pressure threshold.
 * 
 * @return float Current threshold value
 */
float valve_control_get_threshold(void);

/**
 * @brief Retrieve current valve state (true = HIGH/OPEN, false = LOW/CLOSED).
 * 
 * @return true Valve active (HIGH)
 * @return false Valve inactive (LOW)
 */
bool valve_control_get_state(void);

/**
 * @brief Evaluate pressure reading against current threshold and update output pin.
 * 
 * @param pressure Current pressure reading
 * @return true Valve active (HIGH)
 * @return false Valve inactive (LOW)
 */
bool valve_control_evaluate(float pressure);

#ifdef __cplusplus
}
#endif

#endif // VALVE_CONTROL_H
