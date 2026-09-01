#ifndef MQTT_TELEMETRY_H
#define MQTT_TELEMETRY_H

#include "esp_err.h"
#include "sensor_record.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UBIDOTS_BROKER_URI    "mqtt://industrial.api.ubidots.com:1883"
#define UBIDOTS_TOKEN         "BBUS-DSDYa8l30k02zpTkD9MxV633fQVcKI"
#define UBIDOTS_DEVICE_ID     "6a7c5e3aa9e66fc6fba1fe2b"
#define UBIDOTS_DEVICE_LABEL  "reliable-esp"
#define UBIDOTS_PUB_TOPIC     "/v1.6/devices/reliable-esp"

/**
 * @brief Initialize Ubidots MQTT telemetry client.
 */
esp_err_t mqtt_telemetry_init(const char *broker_uri, const char *token, const char *device_label);

/**
 * @brief Check if MQTT broker connection to Ubidots is active.
 */
bool mqtt_telemetry_is_connected(void);

/**
 * @brief Publish a single sensor record as an Ubidots-compliant timestamped JSON payload over MQTT.
 */
esp_err_t mqtt_telemetry_publish_record(const sensor_record_t *record);

/**
 * @brief FreeRTOS task body for telemetry management & offline store-and-forward backlog draining.
 */
void mqtt_telemetry_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // MQTT_TELEMETRY_H
