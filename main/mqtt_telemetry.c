#include "mqtt_telemetry.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "UBIDOTS_TELEMETRY";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_mqtt_connected = false;
static char s_pub_topic[128] = UBIDOTS_PUB_TOPIC;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Successfully Connected to Ubidots MQTT Broker!");
        s_is_mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from Ubidots MQTT Broker");
        s_is_mqtt_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "Ubidots Telemetry Payload Delivered (msg_id=%d)", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Ubidots MQTT Event Error encountered");
        break;
    default:
        break;
    }
}

esp_err_t mqtt_telemetry_init(const char *broker_uri, const char *token, const char *device_label)
{
    const char *uri = broker_uri ? broker_uri : UBIDOTS_BROKER_URI;
    const char *auth_token = token ? token : UBIDOTS_TOKEN;
    const char *dev_label = device_label ? device_label : UBIDOTS_DEVICE_LABEL;

    snprintf(s_pub_topic, sizeof(s_pub_topic), "/v1.6/devices/%s", dev_label);
    ESP_LOGI(TAG, "Initializing Ubidots MQTT Client [Broker: %s, Device: %s, Topic: %s]",
             uri, dev_label, s_pub_topic);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = uri
            }
        },
        .credentials = {
            .username = auth_token,
            .client_id = dev_label,
            .authentication = {
                .password = auth_token
            }
        }
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize ESP-IDF MQTT client for Ubidots");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    return ESP_OK;
}

bool mqtt_telemetry_is_connected(void)
{
    return s_is_mqtt_connected;
}

esp_err_t mqtt_telemetry_publish_record(const sensor_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    uint64_t timestamp_ms = record->timestamp_us / 1000;
    // Check if timestamp is a real Unix Epoch timestamp (e.g. after year 2020)
    bool has_valid_epoch = (timestamp_ms > 1600000000000ULL);

    if (has_valid_epoch) {
        // Temperature variable object with epoch timestamp
        cJSON *temp_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(temp_obj, "value", record->temperature);
        cJSON_AddNumberToObject(temp_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "temperature", temp_obj);

        // Humidity variable object with epoch timestamp
        cJSON *hum_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(hum_obj, "value", record->humidity);
        cJSON_AddNumberToObject(hum_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "humidity", hum_obj);

        // Battery Voltage variable object with epoch timestamp
        cJSON *batt_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(batt_obj, "value", record->battery_mv);
        cJSON_AddNumberToObject(batt_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "battery", batt_obj);
    } else {
        // Flat key-value payload: Ubidots automatically assigns current server timestamp
        cJSON_AddNumberToObject(root, "temperature", record->temperature);
        cJSON_AddNumberToObject(root, "humidity", record->humidity);
        cJSON_AddNumberToObject(root, "battery", record->battery_mv);
    }

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) return ESP_ERR_NO_MEM;

    if (s_is_mqtt_connected && s_mqtt_client) {
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_pub_topic, json_string, 0, 1, 0);
        ESP_LOGI(TAG, "Published Record #%lu to Ubidots '%s' (msg_id=%d): %s",
                 record->record_id, s_pub_topic, msg_id, json_string);
        free(json_string);
        return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
    }

    free(json_string);
    return ESP_ERR_INVALID_STATE;
}

void mqtt_telemetry_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Ubidots Telemetry Task starting on Core %d", xPortGetCoreID());
    esp_task_wdt_add(NULL);

    while (1) {
        if (s_is_mqtt_connected && storage_get_pending_count() > 0) {
            ESP_LOGI(TAG, "Ubidots Connection Active: Draining %lu offline backlogged records from LittleFS...",
                     storage_get_pending_count());

            sensor_record_t backlog_record;
            uint32_t drained_count = 0;

            while (s_is_mqtt_connected && storage_read_oldest_record(&backlog_record) == ESP_OK) {
                if (mqtt_telemetry_publish_record(&backlog_record) == ESP_OK) {
                    storage_commit_tail_advance();
                    drained_count++;
                    vTaskDelay(pdMS_TO_TICKS(50)); // Yield to prevent buffer bloat
                } else {
                    ESP_LOGW(TAG, "Failed to publish backlog record #%lu to Ubidots, pausing drain", backlog_record.record_id);
                    break;
                }
            }

            if (drained_count > 0) {
                ESP_LOGI(TAG, "Successfully drained %lu offline records to Ubidots dashboard", drained_count);
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
