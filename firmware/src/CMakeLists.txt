get_filename_component(OPENWATTS_COMPONENT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
file(READ "${OPENWATTS_COMPONENT_DIR}/../../openwatts-logo.svg" OPENWATTS_LOGO_SVG)
configure_file("${OPENWATTS_COMPONENT_DIR}/logo_asset.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/logo_asset.cpp" @ONLY)

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
        "battery_policy.cpp"
        "calibration.cpp"
        "report_policy.cpp"
        "operating_mode.cpp"
        "led_status.cpp"
        "ride_log.cpp"
        "ride_zero.cpp"
        "road_speed_model.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/logo_asset.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt driver esp_adc esp_event esp_http_server esp_netif esp_timer esp_wifi mqtt app_update esp_https_ota nvs_flash mdns
)
