# Industrial Reliable ESP32 IoT Data Logger with Ubidots Cloud Telemetry, LittleFS Store-and-Forward & Secure Dual-Partition HTTPS OTA Upgrades

An industrial-grade, fault-tolerant ESP32 IoT data logging and telemetry platform built with **ESP-IDF v5.4**, **FreeRTOS dual-core multitasking**, **CRC-16 checksum validation**, **LittleFS circular store-and-forward flash buffer**, **Ubidots REST API cloud telemetry**, and **dual-partition HTTPS Over-The-Air (OTA) firmware upgrade engine** with anti-rollback safeguards.

---

## 🏗️ Architecture & Dual-Core System Design

```
                                  ESP32 DUAL-CORE PROCESSOR
          +-----------------------------------+  +-----------------------------------+
          |          CORE 1 (PRO CPU)         |  |          CORE 0 (APP CPU)         |
          |                                   |  |                                   |
          |  +-----------------------------+  |  |  +-----------------------------+  |
          |  |    sensor_task (Prio 5)     |  |  |  |    storage_task (Prio 4)    |  |
          |  |  Reads DHT22 GPIO @ 0.2Hz   |  |  |  | Writes to LittleFS Flash   |  |
          |  +--------------+--------------+  |  |  +-----------------------------+  |
          +-----------------|-----------------+  |  +-----------------------------+  |
                            |                    |  |   telemetry_task (Prio 3)   |  |
                    Pushes  |                    |  |   Sends HTTP POST Requests  |  |
                    32-Byte |                    |  +-----------------------------+  |
                    Struct  |                    |  +-----------------------------+  |
                            v                    |  |   sys_health_task (Prio 2)  |  |
                 [ FreeRTOS xSensorQueue ] ------+->|   Feeds Task Watchdog WDT   |  |
                     (Depth: 32 items)           |  +-----------------------------+  |
                                                 |  +-----------------------------+  |
                                                 |  |      ota_task (Prio 5)      |  |
                                                 |  |   Secure HTTPS Firmware DL  |  |
                                                 |  +-----------------------------+  |
          +-----------------------------------+  +-----------------------------------+
```

---

## 🚀 Key Subsystems & Features

1. **Dual-Core Task Isolation**:
   * **Core 1**: Real-time microsecond-level DHT22 bit-banging (`sensor_task`, Priority 5).
   * **Core 0**: Asynchronous I/O, LittleFS flash operations (`storage_task`), HTTP REST API networking (`telemetry_task`), TWDT watchdog supervision (`sys_health_task`), and background HTTPS OTA upgrades (`ota_task`).
2. **Inter-Task Communication (IPC)**:
   * Thread-safe FreeRTOS queue (`xSensorQueue`, depth 32 entries).
   * 32-byte packed binary struct (`sensor_record_t`) with MODBUS CRC-16 checksum verification.
3. **Cloud Telemetry**:
   * Ubidots HTTP REST API telemetry client delivering `temperature`, `humidity`, `battery`, and `record_id`.
4. **Offline Store-and-Forward Engine**:
   * LittleFS persistent flash storage (`/littlefs/log_XXXX.bin`).
   * Automatic backlog draining upon Wi-Fi reconnection in strict FIFO order with zero data loss.
5. **Secure Dual-Partition HTTPS OTA Upgrade Engine**:
   * **Ping-Pong Dual Slots**: Alternating firmware flashing between `ota_0` (0x10000) and `ota_1` (0x190000).
   * **Mozilla Root CA Validation**: Embedded `esp_crt_bundle_attach` with mbedTLS for TLS 1.3 encryption over public HTTPS tunnels (Let's Encrypt / Pinggy).
   * **Progressive Streaming**: `esp_https_ota_perform` non-blocking download with real-time percentage progress logging.
   * **Anti-Rollback Protection**: Startup self-testing via `esp_ota_mark_app_valid_cancel_rollback()`.

---

## 📊 Flash Memory & Partition Table (`partitions.csv`)

| Partition Label | Type | SubType | Offset | Size | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `nvs` | `data` | `nvs` | `0x009000` | 16 KB | Non-Volatile Storage (Wi-Fi credentials, state) |
| `otadata` | `data` | `ota` | `0x00D000` | 8 KB | Active boot slot selector & rollback registry |
| `phy_init` | `data` | `phy` | `0x00F000` | 4 KB | RF calibration data |
| `ota_0` | `app` | `ota_0` | `0x010000` | 1.5 MB | Primary Application Firmware Slot |
| `ota_1` | `app` | `ota_1` | `0x190000` | 1.5 MB | Secondary Application Firmware Slot (OTA Target) |
| `storage` | `data` | `spiffs` | `0x310000` | 960 KB | LittleFS Offline Store-and-Forward Log Files |

---

## 🔌 Hardware Pin Mapping & Circuit Netlist

| ESP32 Pin | Component Pin | Function / Wire Color | Notes |
| :--- | :--- | :--- | :--- |
| **GPIO 2** | Green LED `A` | **Stage 1 Boot LED** (v1.0 Active) | Via 220Ω resistor `r1` |
| **GPIO 3** | Yellow LED `A` | **Stage 2 1st OTA LED** (v2.0 Active) | Via 220Ω resistor `r2` |
| **GPIO 4** | Blue LED `A` | **Stage 3 2nd OTA LED** (v3.0 Active) | Via 220Ω resistor `r3` |
| **GPIO 5** | DHT22 `SDA` | Sensor 1-Wire Data Line | 10k internal pullup |
| **GPIO 13**| Pushbutton `1.l` | Force Network Disconnect Test | Active Low (pullup) |
| **3V3** | DHT22 `VCC` | Power (3.3V) | Red wire |
| **GND** | DHT22 `GND`, LEDs `C`, Btn `2.l` | Ground | Black wires |

---

## 🛠️ Step-by-Step Guide: Running the OTA Server & Testing

### 1. Load ESP-IDF Environment
In your terminal, load the ESP-IDF toolchain:
```bash
get_idf
# Or manually:
. $HOME/esp/v5.4/esp-idf/export.sh
```

### 2. Start the Local Firmware Server
Inside the project root directory, launch Python's built-in HTTP server on port 8070:
```bash
cd ~/esp/reliable_data_logger
python3 -m http.server 8070
```

### 3. Open the Public HTTPS Tunnel (Pinggy)
In a second terminal window, open a secure tunnel pointing to your local port:
```bash
ssh -p 443 -R0:localhost:8070 -o StrictHostKeyChecking=no qr@a.pinggy.io
```
Copy the public HTTPS link provided (e.g. `https://phahn-xxxx.run.pinggy-free.link`).

### 4. Configure the Firmware URL & Compile
1. In `main/main.c`, set the `DEFAULT_OTA_URL`:
   ```c
   #define DEFAULT_OTA_URL "https://phahn-xxxx.run.pinggy-free.link/firmware.bin"
   ```
2. Build the project and copy the binary for the server:
   ```bash
   idf.py build
   cp build/reliable_data_logger.bin firmware.bin
   ```

### 5. Run the Simulation in VS Code Wokwi
1. Open `diagram.json` in VS Code.
2. Press **Play / Start Simulation**.
3. Watch the serial monitor:
   * ESP32 connects to `Wokwi-GUEST`.
   * Background `ota_task` downloads the new firmware over HTTPS (`OTA Progress: 20%... 40%... 60%... 80%... 100%`).
   * Firmware flashes into the alternate slot (`ota_1`).
   * ESP32 restarts automatically and boots into the upgraded firmware!

---

## ⚡ Flashing to Real Physical ESP32 Hardware

To flash directly to a physical ESP32 board over USB:
```bash
cd ~/esp/reliable_data_logger
idf.py -p /dev/ttyUSB0 flash monitor
```
*(Press `Ctrl+]` to exit the serial monitor).*

