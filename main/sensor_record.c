#include "sensor_record.h"
#include <stddef.h>

uint16_t sensor_record_calc_crc(const sensor_record_t *record)
{
    if (!record) return 0;

    const uint8_t *data = (const uint8_t *)record;
    size_t length = sizeof(sensor_record_t) - sizeof(uint16_t); // Exclude crc16 field at the end
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
