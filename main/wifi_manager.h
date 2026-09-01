#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_BACKOFF
} wifi_state_t;

/**
 * @brief Initialize Wi-Fi station mode with self-healing exponential backoff reconnect logic.
 */
esp_err_t wifi_manager_init(const char *ssid, const char *password);

/**
 * @brief Check if Wi-Fi has IP allocation and active link.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get current Wi-Fi state machine status.
 */
wifi_state_t wifi_manager_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
