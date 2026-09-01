#ifndef SENSOR_RECORD_H
#define SENSOR_RECORD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread-safe binary sensor data record structure (32 bytes aligned)
 */
typedef struct __attribute__((packed)) {
    uint64_t timestamp_us;   /*!< Microseconds epoch timestamp */
    uint32_t record_id;      /*!< Monotonically increasing record ID */
    float temperature;       /*!< Temperature in Celsius */
    float humidity;          /*!< Relative Humidity in % */
    float pressure;          /*!< Atmospheric Pressure in hPa */
    uint16_t battery_mv;     /*!< System supply voltage in millivolts */
    uint8_t sensor_status;   /*!< Sensor operational bitmask flags (0x01: OK, 0x02: DHT_ERR, 0x04: BATT_LOW) */
    uint8_t reserved;        /*!< Alignment padding byte */
    uint16_t crc16;          /*!< CRC-16 payload integrity checksum */
} sensor_record_t;

/**
 * @brief Calculates CRC-16 MODBUS checksum over a sensor_record_t excluding the crc16 field.
 * 
 * @param record Pointer to sensor record struct
 * @return uint16_t Calculated CRC-16 checksum
 */
uint16_t sensor_record_calc_crc(const sensor_record_t *record);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_RECORD_H
