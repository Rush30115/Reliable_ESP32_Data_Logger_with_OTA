#ifndef DISPLAY_TEST_H
#define DISPLAY_TEST_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Default I2C configuration for OLED testing
#define DISPLAY_TEST_I2C_PORT       I2C_NUM_0
#define DISPLAY_TEST_DEFAULT_SDA    GPIO_NUM_21
#define DISPLAY_TEST_DEFAULT_SCL    GPIO_NUM_22
#define DISPLAY_TEST_CLK_SPEED_HZ   100000 // 100 kHz Standard Mode for maximum wire resilience

/**
 * @brief Diagnostic report structure returned by display test suite.
 */
typedef struct {
    bool i2c_bus_initialized;
    gpio_num_t active_sda;
    gpio_num_t active_scl;
    int total_devices_found;
    uint8_t detected_addrs[16];
    bool oled_detected;
    uint8_t oled_addr;               // 0x3C or 0x3D
    bool wires_swapped_detected;     // True if devices found only when SDA & SCL were inverted
    bool charge_pump_verified;
    bool pattern_test_completed;
    esp_err_t last_error;
    char diag_message[256];
} display_test_report_t;

/**
 * @brief Scans an I2C bus across addresses 0x01 to 0x7F.
 * 
 * @param port I2C port number
 * @param found_addrs Output buffer for found 7-bit addresses
 * @param max_addrs Size of output buffer
 * @return int Number of devices that acknowledged (ACK)
 */
int display_test_scan_i2c_bus(i2c_port_t port, uint8_t *found_addrs, int max_addrs);

/**
 * @brief Run full automated hardware diagnostic test on the OLED display.
 * 
 * 1. Probes I2C on configured SDA/SCL.
 * 2. If nothing found, tests swapped SDA/SCL to detect inverted wiring.
 * 3. Identifies SSD1306 I2C address (0x3C vs 0x3D).
 * 4. Runs SSD1306 power-on & charge pump verification.
 * 5. Executes visual test sequence:
 *      - Stage 1: All Pixels ON (Screen Flash / Power Check)
 *      - Stage 2: Checkerboard Pattern (Pixel Matrix Check)
 *      - Stage 3: Display Clear
 *      - Stage 4: Test Banner & Hardware Status Screen
 * 
 * @param sda_pin GPIO pin for SDA
 * @param scl_pin GPIO pin for SCL
 * @param report Output pointer to receive detailed diagnostic findings
 * @return esp_err_t ESP_OK if display detected and operational, error code otherwise
 */
esp_err_t display_test_run(gpio_num_t sda_pin, gpio_num_t scl_pin, display_test_report_t *report);

/**
 * @brief Execute a quick visual test cycle on an already-initialized OLED.
 * 
 * Cycles through:
 * 1. All ON (1.5 seconds)
 * 2. Checkerboard (1.5 seconds)
 * 3. Diagnostic Info Screen
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_test_cycle_patterns(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_TEST_H
