#ifndef HTTP_TELEMETRY_H
#define HTTP_TELEMETRY_H

#include "esp_err.h"
#include "sensor_record.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Centralized Ubidots HTTP REST API Cloud Endpoint Parameters
#define UBIDOTS_HTTP_URL          "http://industrial.api.ubidots.com/api/v1.6/devices/reliable-esp"
#define UBIDOTS_TOKEN             "BBUS-DSDYa8l30k02zpTkD9MxV633fQVcKI"
#define UBIDOTS_DEVICE_LABEL      "reliable-esp"
#define HTTP_DRAIN_INTERVAL_MS    100 // 100ms yield delay for fast Ubidots HTTP FIFO backlog drain

/**
 * @brief Initialize Ubidots HTTP REST API telemetry client engine.
 */
esp_err_t http_telemetry_init(const char *url, const char *token);

/**
 * @brief Check if Wi-Fi network link is ready for HTTP request execution.
 */
bool http_telemetry_is_connected(void);

/**
 * @brief Publish a single sensor record as a JSON payload to Ubidots via HTTP POST.
 */
esp_err_t http_telemetry_publish_record(const sensor_record_t *record);

/**
 * @brief FreeRTOS task body for Ubidots HTTP telemetry management & offline store-and-forward backlog draining.
 */
void http_telemetry_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // HTTP_TELEMETRY_H
