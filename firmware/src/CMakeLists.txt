idf_component_register(
    SRCS
        "main.cpp"
        "board.cpp"
        "hx711.cpp"
        "imu_lsm6ds3.cpp"
        "cadence_estimator.cpp"
        "power_estimator.cpp"
        "power_manager.cpp"
        "ble_cycling_power.cpp"
        "settings_storage.cpp"
        "setup_wifi.cpp"
        "web_ui.cpp"
        "mqtt_notifier.cpp"
        "self_test.cpp"
        "runtime.cpp"
        "battery_policy.cpp"
        "report_policy.cpp"
        "rotation.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt driver esp_adc esp_event esp_http_server esp_netif esp_timer esp_wifi mqtt app_update esp_https_ota nvs_flash
)
