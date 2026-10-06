#ifndef SHT40_H
#define SHT40_H

#include "esp_err.h"
#include "driver/i2c.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 7Semi SHT40 Temperature and Humidity Probe Sensor I2C Address
#define SHT40_I2C_ADDR          0x44

// SHT40 Command Set per Sensirion Datasheet
#define SHT40_CMD_MEASURE_HIGH  0xFD // High repeatability measurement (~8.2ms)
#define SHT40_CMD_MEASURE_MED   0xF6 // Medium repeatability measurement (~4.5ms)
#define SHT40_CMD_MEASURE_LOW   0xE0 // Low repeatability measurement (~1.7ms)
#define SHT40_CMD_READ_SERIAL   0x89 // Read serial number
#define SHT40_CMD_SOFT_RESET    0x94 // Soft reset

/**
 * @brief Probe and initialize the SHT40 sensor on the specified I2C bus.
 * 
 * @param port I2C port number (e.g. I2C_NUM_0)
 * @return esp_err_t ESP_OK if SHT40 responds with ACK, error code otherwise
 */
esp_err_t sht40_init(i2c_port_t port);

/**
 * @brief Read precision temperature and relative humidity from SHT40.
 * 
 * @param port I2C port number
 * @param[out] temp_c Temperature in degrees Celsius (-40 to 125 C)
 * @param[out] hum_pct Relative humidity in percentage (0 to 100 %)
 * @return esp_err_t ESP_OK on valid CRC-checked reading, error code otherwise
 */
esp_err_t sht40_read(i2c_port_t port, float *temp_c, float *hum_pct);

/**
 * @brief Check if SHT40 sensor is actively detected on the I2C bus.
 */
bool sht40_is_detected(void);

#ifdef __cplusplus
}
#endif

#endif // SHT40_H
