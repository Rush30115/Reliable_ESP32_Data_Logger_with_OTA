#include "storage_engine.h"
#include "sensor_record.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_task_wdt.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "STORAGE_ENGINE";

extern QueueHandle_t xSensorQueue;
SemaphoreHandle_t xFlashMutex = NULL;

static uint32_t s_head_index = 0;
static uint32_t s_tail_index = 0;
static uint32_t s_pending_records = 0;

static bool s_littlefs_mounted = false;

static esp_err_t save_nvs_indices(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage_idx", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u32(handle, "head", s_head_index);
        nvs_set_u32(handle, "tail", s_tail_index);
        nvs_set_u32(handle, "count", s_pending_records);
        nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

static esp_err_t load_nvs_indices(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage_idx", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_u32(handle, "head", &s_head_index);
        nvs_get_u32(handle, "tail", &s_tail_index);
        nvs_get_u32(handle, "count", &s_pending_records);
        nvs_close(handle);
        ESP_LOGI(TAG, "Loaded storage indices from NVS: Head=%lu, Tail=%lu, Count=%lu",
                 s_head_index, s_tail_index, s_pending_records);
    } else {
        ESP_LOGI(TAG, "Initializing fresh NVS storage indices...");
        s_head_index = 0;
        s_tail_index = 0;
        s_pending_records = 0;
        save_nvs_indices();
    }
    return ESP_OK;
}

esp_err_t storage_engine_init(void)
{
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Create Mutex
    xFlashMutex = xSemaphoreCreateMutex();
    if (!xFlashMutex) return ESP_FAIL;

    // 3. Mount LittleFS partition
    esp_vfs_littlefs_conf_t conf = {
        .base_path = LITTLEFS_MOUNT_POINT,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false
    };

    ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "LittleFS 'storage' partition not found in partition table. Operating in direct telemetry mode.");
            s_littlefs_mounted = false;
            load_nvs_indices();
            return ESP_OK;
        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    s_littlefs_mounted = true;
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS Partition Mounted Successfully: Total %d KB, Used %d KB", total / 1024, used / 1024);
    }

    load_nvs_indices();
    return ESP_OK;
}

esp_err_t storage_write_record(const sensor_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    if (!s_littlefs_mounted) return ESP_ERR_NOT_SUPPORTED;

    if (xSemaphoreTake(xFlashMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/log_%04lu.bin", LITTLEFS_MOUNT_POINT, s_head_index % MAX_LOG_FILES);

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open LittleFS file %s for writing", filepath);
        xSemaphoreGive(xFlashMutex);
        return ESP_FAIL;
    }

    size_t written = fwrite(record, sizeof(sensor_record_t), 1, f);
    fclose(f);

    if (written == 1) {
        s_head_index++;
        s_pending_records++;
        save_nvs_indices();
        ESP_LOGI(TAG, "Offline Record #%lu written to %s (Pending Count=%lu)",
                 record->record_id, filepath, s_pending_records);
    } else {
        ESP_LOGE(TAG, "Failed to write record to file %s", filepath);
    }

    xSemaphoreGive(xFlashMutex);
    return (written == 1) ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_read_oldest_record(sensor_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    if (s_pending_records == 0) return ESP_ERR_NOT_FOUND;

    if (xSemaphoreTake(xFlashMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/log_%04lu.bin", LITTLEFS_MOUNT_POINT, s_tail_index % MAX_LOG_FILES);

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Log file %s not found for read", filepath);
        xSemaphoreGive(xFlashMutex);
        return ESP_ERR_NOT_FOUND;
    }

    size_t read_bytes = fread(record, sizeof(sensor_record_t), 1, f);
    fclose(f);

    xSemaphoreGive(xFlashMutex);

    if (read_bytes == 1) {
        // Validate CRC integrity
        uint16_t calc_crc = sensor_record_calc_crc(record);
        if (record->crc16 != calc_crc) {
            ESP_LOGE(TAG, "Corrupted offline record in %s (CRC rx 0x%04X != calc 0x%04X)",
                     filepath, record->crc16, calc_crc);
            storage_commit_tail_advance(); // Skip corrupted record
            return ESP_ERR_INVALID_CRC;
        }
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t storage_commit_tail_advance(void)
{
    if (xSemaphoreTake(xFlashMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/log_%04lu.bin", LITTLEFS_MOUNT_POINT, s_tail_index % MAX_LOG_FILES);

    // Remove file after successful transmission
    remove(filepath);

    s_tail_index++;
    if (s_pending_records > 0) s_pending_records--;
    save_nvs_indices();

    ESP_LOGI(TAG, "Advanced tail index to %lu (Pending Count=%lu)", s_tail_index, s_pending_records);

    xSemaphoreGive(xFlashMutex);
    return ESP_OK;
}

uint32_t storage_get_pending_count(void)
{
    return s_pending_records;
}

// Forward declaration from telemetry engine
extern bool http_telemetry_is_connected(void);
extern esp_err_t http_telemetry_publish_record(const sensor_record_t *record);

void storage_engine_task(void *pvParameters)
{
    sensor_record_t record;
    ESP_LOGI(TAG, "Storage Engine Task starting on Core %d", xPortGetCoreID());

    esp_task_wdt_add(NULL);

    while (1) {
        // Dequeue sensor record from queue with 500ms block
        if (xQueueReceive(xSensorQueue, &record, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (http_telemetry_is_connected()) {
                // Online mode: direct telemetry publication
                ESP_LOGI(TAG, "Online mode: Routing Record #%lu directly to HTTP telemetry", record.record_id);
                if (http_telemetry_publish_record(&record) != ESP_OK) {
                    // Fallback to flash buffer if direct publication fails
                    storage_write_record(&record);
                }
            } else {
                // Offline mode: store in LittleFS persistent circular buffer
                ESP_LOGW(TAG, "Offline mode: Buffering Record #%lu to LittleFS circular storage", record.record_id);
                storage_write_record(&record);
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
