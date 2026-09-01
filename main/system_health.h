#ifndef SYSTEM_HEALTH_H
#define SYSTEM_HEALTH_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Task Watchdog Timer (TWDT) supervision subsystem.
 */
esp_err_t system_health_init(uint32_t timeout_seconds);

/**
 * @brief FreeRTOS task body for heap health monitoring and TWDT maintenance.
 */
void system_health_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_HEALTH_H
