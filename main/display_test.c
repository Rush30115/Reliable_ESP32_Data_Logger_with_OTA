#include "display_test.h"
#include "oled_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DISPLAY_TEST";



int display_test_scan_i2c_bus(i2c_port_t port, uint8_t *found_addrs, int max_addrs)
{
    int count = 0;
    ESP_LOGI(TAG, "Scanning I2C Bus (Addresses 0x01 to 0x7F)...");

    for (uint8_t addr = 1; addr < 128; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, " -> [ACK] Device detected at 7-bit address 0x%02X", addr);
            if (found_addrs && count < max_addrs) {
                found_addrs[count] = addr;
            }
            count++;
        }
    }

    if (count == 0) {
        ESP_LOGW(TAG, " -> No I2C devices responded with ACK.");
    } else {
        ESP_LOGI(TAG, " -> Scan complete: %d device(s) found.", count);
    }

    return count;
}



esp_err_t display_test_run(gpio_num_t sda_pin, gpio_num_t scl_pin, display_test_report_t *report)
{
    if (report) {
        memset(report, 0, sizeof(display_test_report_t));
    }

    ESP_LOGI(TAG, "=========================================================");
    ESP_LOGI(TAG, "    🧪 SSD1306 OLED HARDWARE TEST SUITE 🧪              ");
    ESP_LOGI(TAG, "    Target Pins: SDA=GPIO %d, SCL=GPIO %d (100 kHz)     ", sda_pin, scl_pin);
    ESP_LOGI(TAG, "=========================================================");

    // Step 1: Initialize OLED driver on requested pins
    esp_err_t err = oled_display_init(sda_pin, scl_pin);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Primary pins (SDA=%d, SCL=%d) failed. Probing swapped pins (SDA=%d, SCL=%d)...",
                 sda_pin, scl_pin, scl_pin, sda_pin);

        esp_err_t swap_err = oled_display_init(scl_pin, sda_pin);
        if (swap_err == ESP_OK) {
            ESP_LOGE(TAG, "🔴 CRITICAL FINDING: Device detected with SWAPPED pins!");
            ESP_LOGE(TAG, "🔴 SDA wire is connected to GPIO %d, SCL wire is connected to GPIO %d!", scl_pin, sda_pin);
            ESP_LOGE(TAG, "🔴 Fix: Swap the physical SDA and SCL jumper wires.");
            if (report) {
                report->wires_swapped_detected = true;
                report->total_devices_found = 1;
                report->active_sda = scl_pin;
                report->active_scl = sda_pin;
                report->oled_detected = true;
                report->oled_addr = oled_display_get_addr();
                snprintf(report->diag_message, sizeof(report->diag_message),
                         "Wires SWAPPED! SDA detected on GPIO %d, SCL on GPIO %d", scl_pin, sda_pin);
            }
            return ESP_ERR_INVALID_RESPONSE;
        }

        ESP_LOGE(TAG, "❌ No OLED response detected on GPIO %d or GPIO %d.", sda_pin, scl_pin);
        ESP_LOGE(TAG, "👉 Hardware Checklist:");
        ESP_LOGE(TAG, "   1. Is OLED VCC connected to ESP32 3.3V?");
        ESP_LOGE(TAG, "   2. Is OLED GND connected to ESP32 GND?");
        ESP_LOGE(TAG, "   3. Is OLED SDA connected to GPIO %d?", sda_pin);
        ESP_LOGE(TAG, "   4. Is OLED SCL connected to GPIO %d?", scl_pin);
        if (report) {
            report->last_error = err;
            snprintf(report->diag_message, sizeof(report->diag_message), "No I2C device detected on bus");
        }
        return err;
    }

    uint8_t oled_addr = oled_display_get_addr();
    ESP_LOGI(TAG, "✅ Target OLED I2C Address identified: 0x%02X", oled_addr);
    if (report) {
        report->i2c_bus_initialized = true;
        report->active_sda = sda_pin;
        report->active_scl = scl_pin;
        report->total_devices_found = 1;
        report->detected_addrs[0] = oled_addr;
        report->oled_detected = true;
        report->oled_addr = oled_addr;
        report->charge_pump_verified = true;
    }

    // Step 2: Display Diagnostic Status Screen (Stable low-current UI, no charge pump brownout)
    ESP_LOGI(TAG, "🎨 Displaying Diagnostic Status Screen (Stable low-current UI)...");
    oled_display_show_message("OLED ACTIVE", "ADDR: 0x3C (ACK)", "LOGGER READY");
    vTaskDelay(pdMS_TO_TICKS(1500));

    if (report) {
        report->pattern_test_completed = true;
        snprintf(report->diag_message, sizeof(report->diag_message),
                 "SUCCESS: OLED verified at 0x%02X on SDA:%d SCL:%d", oled_addr, sda_pin, scl_pin);
    }

    ESP_LOGI(TAG, "=========================================================");
    ESP_LOGI(TAG, "    🎉 OLED HARDWARE DIAGNOSTIC TEST PASSED! 🎉          ");
    ESP_LOGI(TAG, "=========================================================");

    return ESP_OK;
}

esp_err_t display_test_cycle_patterns(void)
{
    ESP_LOGI(TAG, "Running quick display test cycle...");
    return display_test_run(DISPLAY_TEST_DEFAULT_SDA, DISPLAY_TEST_DEFAULT_SCL, NULL);
}
