#ifndef STORAGE_ENGINE_H
#define STORAGE_ENGINE_H

#include "esp_err.h"
#include "sensor_record.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LITTLEFS_MOUNT_POINT "/littlefs"
#define MAX_LOG_FILES        100

/**
 * @brief Initialize NVS configuration and LittleFS partition.
 */
esp_err_t storage_engine_init(void);

/**
 * @brief Write a sensor record to persistent LittleFS circular log file.
 */
esp_err_t storage_write_record(const sensor_record_t *record);

/**
 * @brief Read the oldest un-acknowledged record from LittleFS circular log file.
 */
esp_err_t storage_read_oldest_record(sensor_record_t *record);

/**
 * @brief Advance the circular tail index after a record is successfully published.
 */
esp_err_t storage_commit_tail_advance(void);

/**
 * @brief Get total pending offline record count.
 */
uint32_t storage_get_pending_count(void);

/**
 * @brief FreeRTOS task body for storage engine.
 */
void storage_engine_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_ENGINE_H
