#include "sht40.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT40_DRIVER";
static bool s_sht40_detected = false;

static uint8_t sht40_calc_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

esp_err_t sht40_init(i2c_port_t port)
{
    // Probe SHT40 at 7-bit address 0x44 with soft reset command
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, SHT40_CMD_SOFT_RESET, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10)); // SHT40 soft reset duration is ~1 ms
        s_sht40_detected = true;
        ESP_LOGI(TAG, "✅ 7Semi SHT40 Temperature & Humidity Probe detected at I2C Address 0x%02X!", SHT40_I2C_ADDR);
    } else {
        s_sht40_detected = false;
        ESP_LOGW(TAG, "SHT40 probe not detected at 0x%02X (%s)", SHT40_I2C_ADDR, esp_err_to_name(ret));
    }
    return ret;
}

bool sht40_is_detected(void)
{
    return s_sht40_detected;
}

esp_err_t sht40_read(i2c_port_t port, float *temp_c, float *hum_pct)
{
    if (!temp_c || !hum_pct) {
        return ESP_ERR_INVALID_ARG;
    }

    // Step 1: Send High-Precision Measurement Command (0xFD)
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, SHT40_CMD_MEASURE_HIGH, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        return ret;
    }

    // Step 2: Wait for measurement conversion (High precision takes 8.2 ms max)
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 3: Read 6 response bytes: [T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC]
    uint8_t data[6] = {0};
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 5, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data[5], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        return ret;
    }

    // Step 4: Validate Sensirion CRC-8 checksums
    if (sht40_calc_crc(&data[0], 2) != data[2]) {
        ESP_LOGE(TAG, "SHT40 Temperature CRC-8 mismatch! Expected 0x%02X, got 0x%02X",
                 data[2], sht40_calc_crc(&data[0], 2));
        return ESP_ERR_INVALID_CRC;
    }
    if (sht40_calc_crc(&data[3], 2) != data[5]) {
        ESP_LOGE(TAG, "SHT40 Humidity CRC-8 mismatch! Expected 0x%02X, got 0x%02X",
                 data[5], sht40_calc_crc(&data[3], 2));
        return ESP_ERR_INVALID_CRC;
    }

    // Step 5: Convert raw 16-bit ticks to physical values per Sensirion Datasheet
    // T [°C] = -45 + 175 * (S_T / 65535)
    // RH [%RH] = -6 + 125 * (S_RH / 65535)
    uint16_t t_ticks = (data[0] << 8) | data[1];
    uint16_t rh_ticks = (data[3] << 8) | data[4];

    float t = -45.0f + 175.0f * ((float)t_ticks / 65535.0f);
    float rh = -6.0f + 125.0f * ((float)rh_ticks / 65535.0f);

    if (rh > 100.0f) rh = 100.0f;
    if (rh < 0.0f)   rh = 0.0f;

    *temp_c = t;
    *hum_pct = rh;
    s_sht40_detected = true;

    return ESP_OK;
}
