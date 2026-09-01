#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "sensor_record.h"
#include "sensor_engine.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "http_telemetry.h"
#include "system_health.h"
#include "esp_https_ota.h"                                               
#include "esp_http_client.h"                                             
#include "esp_ota_ops.h"                                                 
#include "esp_log.h"                                                     
#include "esp_crt_bundle.h"                      
#include "driver/gpio.h"

// =========================================================================
// Firmware Version & Status LED Configuration
// Set FIRMWARE_VERSION:
//   1 -> Boot v1.0: Blinks LED on GPIO 2 (Green LED)
//   2 -> After 1st OTA v2.0: Blinks LED on GPIO 15 (Yellow LED)
//   3 -> After 2nd OTA v3.0: Blinks LED on GPIO 4 (Blue LED)
// =========================================================================
#define FIRMWARE_VERSION 1

#if FIRMWARE_VERSION == 1
    #define ACTIVE_BLINK_PIN    GPIO_NUM_2
    #define FW_VERSION_LABEL    "v1.0 (Initial Boot - GPIO 2 LED)"
#elif FIRMWARE_VERSION == 2
    #define ACTIVE_BLINK_PIN    GPIO_NUM_15
    #define FW_VERSION_LABEL    "v2.0 (1st OTA Upgraded - GPIO 15 LED)"
#elif FIRMWARE_VERSION == 3
    #define ACTIVE_BLINK_PIN    GPIO_NUM_4
    #define FW_VERSION_LABEL    "v3.0 (2nd OTA Upgraded - GPIO 4 LED)"
#else
    #define ACTIVE_BLINK_PIN    GPIO_NUM_2
    #define FW_VERSION_LABEL    "vUnknown"
#endif

static const char *OTA_TAG = "LOGGER_OTA";                               
                                                                             
    // Auto-generated symbols from EMBED_TXTFILES "certs/server_cert.pem"   
extern const uint8_t server_cert_pem_start[]                             
asm("_binary_server_cert_pem_start");                                      
extern const uint8_t server_cert_pem_end[]                               
asm("_binary_server_cert_pem_end");

static const char *TAG = "MAIN_APP";

QueueHandle_t xSensorQueue = NULL;

static void led_blink_task(void *pvParameter)
{
    gpio_num_t pin = (gpio_num_t)(intptr_t)pvParameter;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "LED Blinker active on GPIO %d for %s", pin, FW_VERSION_LABEL);

    int level = 0;
    while (1) {
        gpio_set_level(pin, level);
        level = !level;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#define OTA_POLL_INTERVAL_MS 20000

void ota_task(void *pvParameter)                                         
{                                                                        
    const char *firmware_url = (const char *)pvParameter;                
                                                                         
    while (1) {
        // Wait until Wi-Fi is connected and has obtained an IP              
        while (!wifi_manager_is_connected()) {                               
            vTaskDelay(pdMS_TO_TICKS(1000));                                 
        }                                                                    
                                                                             
        ESP_LOGI(OTA_TAG, "Checking for OTA firmware update at: %s", firmware_url);                                                             
                                                                             
        esp_http_client_config_t http_config = {                             
            .url = firmware_url,                                             
            .crt_bundle_attach = esp_crt_bundle_attach, // Auto-validates Let's Encrypt / Pinggy SSL
            .timeout_ms = 5000,                                             
            .keep_alive_enable = true,                                       
        };                                                                   
                                                                             
        esp_https_ota_config_t ota_config = {                                
            .http_config = &http_config,                                     
        };                                                                   
                                                                             
        esp_https_ota_handle_t https_ota_handle = NULL;
        esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
        if (err == ESP_OK) {
            esp_app_desc_t new_app_desc;
            const esp_app_desc_t *running_app_desc = esp_app_get_description();

            if (esp_https_ota_get_img_desc(https_ota_handle, &new_app_desc) == ESP_OK) {
                if (memcmp(new_app_desc.app_elf_sha256, running_app_desc->app_elf_sha256, sizeof(new_app_desc.app_elf_sha256)) == 0) {
                    ESP_LOGI(OTA_TAG, "Server firmware matches active binary (SHA256 match). System is up-to-date! Skipping OTA.");
                    esp_https_ota_abort(https_ota_handle);
                    vTaskDelay(pdMS_TO_TICKS(OTA_POLL_INTERVAL_MS));
                    continue;
                }
            }

            ESP_LOGI(OTA_TAG, "New firmware detected! HTTPS connection established! Downloading and flashing...");
            int last_logged_percent = -1;

            while (1) {
                err = esp_https_ota_perform(https_ota_handle);
                if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                    break;
                }
                int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
                int total_len = esp_https_ota_get_image_size(https_ota_handle);
                int percent = (total_len > 0) ? (read_len * 100 / total_len) : 0;
                if (percent % 20 == 0 && percent != last_logged_percent) {
                    ESP_LOGI(OTA_TAG, "OTA Progress: %d%% (%d / %d bytes)", percent, read_len, total_len);
                    last_logged_percent = percent;
                }
                vTaskDelay(pdMS_TO_TICKS(20)); // Yields CPU to keep other tasks running smoothly
            }

            if (esp_https_ota_is_complete_data_received(https_ota_handle) == true) {
                esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
                if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
                    ESP_LOGI(OTA_TAG, "OTA Download & Flash Complete! Rebooting into new version in 2s...");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                } else {
                    ESP_LOGE(OTA_TAG, "OTA finish failed (%s)", esp_err_to_name(ota_finish_err));
                }
            } else {
                ESP_LOGW(OTA_TAG, "Complete firmware data was not received. Aborting OTA session.");
                esp_https_ota_abort(https_ota_handle);
            }
        } else {
            ESP_LOGI(OTA_TAG, "No new update or connection pending (%s). Next check in 20s...", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(OTA_POLL_INTERVAL_MS));
    }
}

#define DEFAULT_OTA_URL  "https://siyog-2409-40f2-104a-c422-c8bb-7628-ebdd-fe02.run.pinggy-free.link/firmware.bin"

void app_main(void)
{
    // 0. Check running partition and log active firmware version
    const esp_partition_t *running = esp_ota_get_running_partition();

    ESP_LOGI(TAG, "=================================================================");
    ESP_LOGI(TAG, "   🔥 FIRMWARE %s ACTIVE 🔥", FW_VERSION_LABEL);
    ESP_LOGI(TAG, "   💡 Blinking Status LED on GPIO %d                             ", ACTIVE_BLINK_PIN);
    ESP_LOGI(TAG, "=================================================================");

    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "Active Running Partition: %s (Offset: 0x%08lX)", 
             running->label, (unsigned long)running->address);
    ESP_LOGI(TAG, "--------------------------------------------------");

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(OTA_TAG, "Freshly upgraded image detected! Validating firmware to cancel rollback...");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // 1. Initialize Task Watchdog Timer Supervision (15s timeout)
    ESP_ERROR_CHECK(system_health_init(15));

    // 2. Create Inter-Task Communication Queue (Depth: 32 Sensor Records)
    xSensorQueue = xQueueCreate(32, sizeof(sensor_record_t));
    configASSERT(xSensorQueue != NULL);

    // 3. Initialize Storage Subsystem (NVS & LittleFS Store-and-Forward Engine)
    ESP_ERROR_CHECK(storage_engine_init());

    // 4. Initialize Hardware Sensor Engine (DHT22 on GPIO 5)
    ESP_ERROR_CHECK(sensor_engine_init(SENSOR_GPIO_PIN));

    // 5. Initialize Network Manager (Wi-Fi Station & State Machine)
    ESP_ERROR_CHECK(wifi_manager_init("Wokwi-GUEST", ""));

    // 6. Initialize Telemetry Engine (Ubidots HTTP REST API Client)
    ESP_ERROR_CHECK(http_telemetry_init(UBIDOTS_HTTP_URL, UBIDOTS_TOKEN));

    // 7. Spawn Multi-Tasking Architecture across ESP32 Dual Cores
    // LED Blinker Task (Core 1, Priority 1) - Blinks designated Pin (Pin 2 / 3 / 4)
    xTaskCreatePinnedToCore(
        led_blink_task,
        "led_blink_task",
        2048,
        (void *)(intptr_t)ACTIVE_BLINK_PIN,
        1,
        NULL,
        1
    );

    // Sensor Engine Task (Core 1, Priority 5)
    xTaskCreatePinnedToCore(
        sensor_engine_task,
        "sensor_task",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    // Storage Engine Task (Core 0, Priority 4)
    xTaskCreatePinnedToCore(
        storage_engine_task,
        "storage_task",
        4096,
        NULL,
        4,
        NULL,
        0
    );

    // HTTP Telemetry Task (Core 0, Priority 3)
    xTaskCreatePinnedToCore(
        http_telemetry_task,
        "telemetry_task",
        6144,
        NULL,
        3,
        NULL,
        0
    );

    // System Health Diagnostics Task (Core 0, Priority 2)
    xTaskCreatePinnedToCore(
        system_health_task,
        "sys_health_task",
        3072,
        NULL,
        2,
        NULL,
        0
    );

    // Continuous Background OTA Update Task (Core 0, Priority 1, 20s Polling Loop)
    static const char *url = DEFAULT_OTA_URL;      
    xTaskCreatePinnedToCore(ota_task, "ota_task", 8192, (void *)url, 1, NULL, 0);

    ESP_LOGI(TAG, "All FreeRTOS tasks and hardware subsystems initialized successfully.");
}
