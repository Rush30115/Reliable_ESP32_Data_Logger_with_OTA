idf_component_register(
    SRCS 
        "main.c"
        "sensor_record.c"
        "dht22.c"
        "sensor_engine.c"
        "storage_engine.c"
        "wifi_manager.c"
        "http_telemetry.c"
        "system_health.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "certs/server_cert.pem"
    REQUIRES 
        nvs_flash 
        esp_wifi 
        esp_event 
        esp_http_client 
        json 
        esp_timer 
        driver                               
        esp_https_ota 
        app_update        
        mbedtls 
) 
