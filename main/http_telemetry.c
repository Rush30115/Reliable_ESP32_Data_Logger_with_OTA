#include "http_telemetry.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "UBIDOTS_HTTP";

static char s_server_url[256] = UBIDOTS_HTTP_URL;
static char s_auth_token[128] = UBIDOTS_TOKEN;

esp_err_t http_telemetry_init(const char *url, const char *token)
{
    if (url && strlen(url) > 0) {
        strncpy(s_server_url, url, sizeof(s_server_url) - 1);
    }
    if (token && strlen(token) > 0) {
        strncpy(s_auth_token, token, sizeof(s_auth_token) - 1);
    }

    ESP_LOGI(TAG, "Initialized Ubidots HTTP REST Telemetry Engine [Endpoint: %s]", s_server_url);
    return ESP_OK;
}

bool http_telemetry_is_connected(void)
{
    return wifi_manager_is_connected();
}

esp_err_t http_telemetry_publish_record(const sensor_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    if (!wifi_manager_is_connected()) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    uint64_t timestamp_ms = record->timestamp_us / 1000;
    bool has_valid_epoch = (timestamp_ms > 1600000000000ULL);

    if (has_valid_epoch) {
        cJSON *temp_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(temp_obj, "value", record->temperature);
        cJSON_AddNumberToObject(temp_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "temperature", temp_obj);

        cJSON *hum_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(hum_obj, "value", record->humidity);
        cJSON_AddNumberToObject(hum_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "humidity", hum_obj);

        cJSON *batt_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(batt_obj, "value", record->battery_mv);
        cJSON_AddNumberToObject(batt_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "battery", batt_obj);
    } else {
        cJSON_AddNumberToObject(root, "temperature", record->temperature);
        cJSON_AddNumberToObject(root, "humidity", record->humidity);
        cJSON_AddNumberToObject(root, "battery", record->battery_mv);
        cJSON_AddNumberToObject(root, "record_id", record->record_id);
    }

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = s_server_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 8000,
        .is_async = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize ESP HTTP Client handle");
        free(json_string);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Auth-Token", s_auth_token);
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    if (err == ESP_OK && (status_code >= 200 && status_code < 300)) {
        ESP_LOGI(TAG, "Ubidots HTTP POST Delivered [Status=%d, Record #%lu: Temp=%.1fC, Hum=%.1f%%]: %s",
                 status_code, (unsigned long)record->record_id, record->temperature, record->humidity, json_string);
        free(json_string);
        esp_http_client_cleanup(client);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Ubidots HTTP POST Failed [Err=%s, HTTP Status=%d]", esp_err_to_name(err), status_code);
        free(json_string);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
}

void http_telemetry_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Ubidots HTTP Telemetry Task starting on Core %d", xPortGetCoreID());
    esp_task_wdt_add(NULL);

    while (1) {
        if (wifi_manager_is_connected() && storage_get_pending_count() > 0) {
            ESP_LOGI(TAG, "Wi-Fi Online: Draining %lu backlogged records from LittleFS via Ubidots HTTP FIFO...",
                     storage_get_pending_count());

            sensor_record_t backlog_record;
            uint32_t drained_count = 0;

            while (wifi_manager_is_connected() && storage_read_oldest_record(&backlog_record) == ESP_OK) {
                if (http_telemetry_publish_record(&backlog_record) == ESP_OK) {
                    storage_commit_tail_advance();
                    drained_count++;
                    vTaskDelay(pdMS_TO_TICKS(HTTP_DRAIN_INTERVAL_MS)); // Yield delay
                } else {
                    ESP_LOGW(TAG, "Failed to publish Ubidots HTTP backlog record #%lu, pausing drain",
                             (unsigned long)backlog_record.record_id);
                    break;
                }
            }

            if (drained_count > 0) {
                ESP_LOGI(TAG, "Successfully drained %lu offline records to Ubidots HTTP cloud", drained_count);
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
