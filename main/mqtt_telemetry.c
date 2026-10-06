#include "mqtt_telemetry.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "valve_control.h"
#include "sensor_engine.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT_TELEMETRY";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_mqtt_connected = false;
static char s_pub_topic[128] = UBIDOTS_PUB_TOPIC;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Successfully Connected to MQTT Broker!");
        s_is_mqtt_connected = true;

        // Bi-directional MQTT: Subscribe to threshold configuration topics
        int msg_id1 = esp_mqtt_client_subscribe(s_mqtt_client, MQTT_THRESHOLD_TOPIC, 0);
        int msg_id2 = esp_mqtt_client_subscribe(s_mqtt_client, UBIDOTS_SUB_THRESHOLD_TOPIC, 0);
        ESP_LOGI(TAG, "Subscribed to Threshold Topic '%s' (msg_id=%d) & Ubidots Topic '%s' (msg_id=%d)",
                 MQTT_THRESHOLD_TOPIC, msg_id1, UBIDOTS_SUB_THRESHOLD_TOPIC, msg_id2);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from MQTT Broker");
        s_is_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "📩 MQTT DATA RECEIVED | Topic: %.*s | Payload: %.*s",
                 event->topic_len, event->topic, event->data_len, event->data);

        char topic_buf[128] = {0};
        char payload_buf[128] = {0};
        int t_len = (event->topic_len < (int)sizeof(topic_buf) - 1) ? event->topic_len : (int)sizeof(topic_buf) - 1;
        int p_len = (event->data_len < (int)sizeof(payload_buf) - 1) ? event->data_len : (int)sizeof(payload_buf) - 1;
        memcpy(topic_buf, event->topic, t_len);
        memcpy(payload_buf, event->data, p_len);

        if (strstr(topic_buf, "threshold") != NULL) {
            float new_thresh = 0.0f;
            bool valid = false;

            // Attempt JSON parsing
            cJSON *json = cJSON_Parse(payload_buf);
            if (json) {
                cJSON *item = cJSON_GetObjectItem(json, "threshold");
                if (!item) item = cJSON_GetObjectItem(json, "value");
                if (item && cJSON_IsNumber(item)) {
                    new_thresh = (float)item->valuedouble;
                    valid = true;
                }
                cJSON_Delete(json);
            }

            // Fallback: raw numeric string parsing
            if (!valid) {
                char *endptr = NULL;
                float parsed = strtof(payload_buf, &endptr);
                if (endptr != payload_buf) {
                    new_thresh = parsed;
                    valid = true;
                }
            }

            if (valid) {
                ESP_LOGI(TAG, "⚡ Dynamic Threshold Updated via MQTT: %.2f", new_thresh);
                valve_control_set_threshold(new_thresh);

                // Immediate re-evaluation of valve state against current pressure reading
                float cur_p = sensor_engine_get_last_pressure();
                bool valve_on = valve_control_evaluate(cur_p);
                ESP_LOGI(TAG, "Re-evaluation complete: Current Pressure %.2f vs Threshold %.2f -> Valve %s",
                         cur_p, new_thresh, valve_on ? "HIGH (OPEN)" : "LOW (CLOSED)");
            } else {
                ESP_LOGW(TAG, "Failed to parse threshold payload: '%s'", payload_buf);
            }
        }
        break;
    }

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT Telemetry Payload Delivered (msg_id=%d)", event->msg_id);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Event Error encountered");
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
    ESP_LOGI(TAG, "Initializing MQTT Client [Broker: %s, Device: %s, Pub Topic: %s]",
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
        ESP_LOGE(TAG, "Failed to initialize ESP-IDF MQTT client");
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
    bool has_valid_epoch = (timestamp_ms > 1600000000000ULL);

    float pressure_val = record->pressure;
    float hum_val = record->humidity;
    float batt_val = record->battery_mv;
    int valve_state_val = valve_control_get_state() ? 1 : 0;
    float threshold_val = valve_control_get_threshold();

    if (has_valid_epoch) {
        cJSON *p_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(p_obj, "value", pressure_val);
        cJSON_AddNumberToObject(p_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "pressure", p_obj);

        cJSON *h_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(h_obj, "value", hum_val);
        cJSON_AddNumberToObject(h_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "humidity", h_obj);

        cJSON *b_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(b_obj, "value", batt_val);
        cJSON_AddNumberToObject(b_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "battery", b_obj);

        cJSON *v_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(v_obj, "value", valve_state_val);
        cJSON_AddNumberToObject(v_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "valve_state", v_obj);

        cJSON *t_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(t_obj, "value", threshold_val);
        cJSON_AddNumberToObject(t_obj, "timestamp", (double)timestamp_ms);
        cJSON_AddItemToObject(root, "threshold", t_obj);
    } else {
        cJSON_AddNumberToObject(root, "pressure", pressure_val);
        cJSON_AddNumberToObject(root, "temperature", record->temperature); // alias for backwards compatibility
        cJSON_AddNumberToObject(root, "humidity", hum_val);
        cJSON_AddNumberToObject(root, "battery", batt_val);
        cJSON_AddNumberToObject(root, "valve_state", valve_state_val);
        cJSON_AddNumberToObject(root, "threshold", threshold_val);
    }

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) return ESP_ERR_NO_MEM;

    if (s_is_mqtt_connected && s_mqtt_client) {
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_pub_topic, json_string, 0, 1, 0);
        ESP_LOGI(TAG, "Published Telemetry Record #%lu to '%s' (msg_id=%d): %s",
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
