#include "ble_cycling_power.h"

#include <cstdint>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "ble";
constexpr uint16_t kCyclingPowerServiceUuid = 0x1818;
constexpr uint16_t kCyclingPowerMeasurementUuid = 0x2A63;
constexpr uint16_t kCyclingPowerFeatureUuid = 0x2A65;
constexpr uint16_t kSensorLocationUuid = 0x2A5D;
constexpr uint16_t kOpenWattsDiagnosticsUuid = 0xFFF1;

BleCyclingPowerService *g_service = nullptr;
uint16_t g_measurement_handle = 0;
uint16_t g_feature_handle = 0;
uint16_t g_sensor_location_handle = 0;
uint16_t g_diagnostics_handle = 0;
uint8_t g_addr_type = 0;
char g_diagnostics[160] = "self-test not run";

esp_power_level_t closestPowerLevel(int8_t dbm) {
    if (dbm <= -18) return ESP_PWR_LVL_N18;
    if (dbm <= -15) return ESP_PWR_LVL_N15;
    if (dbm <= -12) return ESP_PWR_LVL_N12;
    if (dbm <= -9) return ESP_PWR_LVL_N9;
    if (dbm <= -6) return ESP_PWR_LVL_N6;
    if (dbm <= -3) return ESP_PWR_LVL_N3;
    if (dbm <= 0) return ESP_PWR_LVL_N0;
    if (dbm <= 3) return ESP_PWR_LVL_P3;
    if (dbm <= 6) return ESP_PWR_LVL_P6;
    return ESP_PWR_LVL_P9;
}

uint32_t featureBits() {
    return 0x00000008UL;  // Crank revolution data supported.
}

ble_uuid16_t service_uuid = BLE_UUID16_INIT(kCyclingPowerServiceUuid);
ble_uuid16_t measurement_uuid = BLE_UUID16_INIT(kCyclingPowerMeasurementUuid);
ble_uuid16_t feature_uuid = BLE_UUID16_INIT(kCyclingPowerFeatureUuid);
ble_uuid16_t sensor_location_uuid = BLE_UUID16_INIT(kSensorLocationUuid);
ble_uuid16_t diagnostics_uuid = BLE_UUID16_INIT(kOpenWattsDiagnosticsUuid);

ble_gatt_chr_def gatt_characteristics[] = {
    {
        .uuid = &measurement_uuid.u,
        .access_cb = BleCyclingPowerService::gattAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_measurement_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &feature_uuid.u,
        .access_cb = BleCyclingPowerService::gattAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = &g_feature_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &sensor_location_uuid.u,
        .access_cb = BleCyclingPowerService::gattAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = &g_sensor_location_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &diagnostics_uuid.u,
        .access_cb = BleCyclingPowerService::gattAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = &g_diagnostics_handle,
        .cpfd = nullptr,
    },
    {nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr},
};

const ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .includes = nullptr,
        .characteristics = gatt_characteristics,
    },
    {0, nullptr, nullptr, nullptr},
};
}  // namespace

void encodeCyclingPowerMeasurement(const PowerSample &sample, uint8_t payload[8]) {
    const uint16_t flags = 0x0020;
    const int16_t safe_power = sample.valid && sample.power_watts > 0 ? sample.power_watts : 0;
    const uint16_t encoded_power = static_cast<uint16_t>(safe_power);
    payload[0] = static_cast<uint8_t>(flags & 0xFF);
    payload[1] = static_cast<uint8_t>(flags >> 8);
    payload[2] = static_cast<uint8_t>(encoded_power & 0xFF);
    payload[3] = static_cast<uint8_t>((encoded_power >> 8) & 0xFF);
    payload[4] = static_cast<uint8_t>(sample.cumulative_crank_revolutions & 0xFF);
    payload[5] = static_cast<uint8_t>(sample.cumulative_crank_revolutions >> 8);
    payload[6] = static_cast<uint8_t>(sample.last_crank_event_time & 0xFF);
    payload[7] = static_cast<uint8_t>(sample.last_crank_event_time >> 8);
}

esp_err_t BleCyclingPowerService::begin(const char *device_name, int8_t advertising_power_dbm) {
    g_service = this;
    configured_power_dbm_ = advertising_power_dbm;
    if (device_name != nullptr && device_name[0] != '\0') {
        std::strncpy(device_name_, device_name, sizeof(device_name_) - 1);
        device_name_[sizeof(device_name_) - 1] = '\0';
    }

    const int init_rc = nimble_port_init();
    if (init_rc != 0) {
        ESP_LOGE(kTag, "nimble_port_init failed: %d", init_rc);
        return ESP_FAIL;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(device_name_);
    const esp_err_t power_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,
                                                     closestPowerLevel(advertising_power_dbm));
    if (power_err != ESP_OK) {
        ESP_LOGW(kTag, "could not set advertising power: %s", esp_err_to_name(power_err));
    }
    ble_hs_cfg.sync_cb = &BleCyclingPowerService::onSync;

    int rc = ble_gatts_count_cfg(gatt_services);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_services);
    if (rc != 0) {
        return ESP_FAIL;
    }

    nimble_port_freertos_init(&BleCyclingPowerService::hostTask);
    return ESP_OK;
}

bool BleCyclingPowerService::connected() const {
    return connected_.load(std::memory_order_relaxed);
}

void BleCyclingPowerService::notify(const PowerSample &sample) {
    if (!connected_.load(std::memory_order_relaxed) || g_measurement_handle == 0) {
        return;
    }

    uint8_t payload[8]{};
    encodeCyclingPowerMeasurement(sample, payload);
    last_notified_power_watts_.store(sample.power_watts, std::memory_order_relaxed);
    last_notified_cadence_x100_.store(static_cast<int32_t>(std::lround(sample.cadence_rpm * 100.0F)),
                                      std::memory_order_relaxed);
    last_notified_crank_revolutions_.store(sample.cumulative_crank_revolutions, std::memory_order_relaxed);
    last_notified_crank_event_time_.store(sample.last_crank_event_time, std::memory_order_relaxed);
    last_notify_us_.store(esp_timer_get_time(), std::memory_order_relaxed);

    os_mbuf *om = ble_hs_mbuf_from_flat(payload, sizeof(payload));
    if (om != nullptr) {
        const int rc = ble_gatts_notify_custom(conn_handle_.load(std::memory_order_relaxed), g_measurement_handle, om);
        last_notify_result_.store(rc, std::memory_order_relaxed);
        if (rc == 0) notify_success_count_.fetch_add(1, std::memory_order_relaxed);
        else {
            notify_failure_count_.fetch_add(1, std::memory_order_relaxed);
            ESP_LOGW(kTag, "CPS notify failed rc=%d power=%d rpm=%.1f revolutions=%u event=%u", rc,
                     static_cast<int>(sample.power_watts), static_cast<double>(sample.cadence_rpm),
                     static_cast<unsigned>(sample.cumulative_crank_revolutions),
                     static_cast<unsigned>(sample.last_crank_event_time));
        }
    } else {
        last_notify_result_.store(BLE_HS_ENOMEM, std::memory_order_relaxed);
        notify_failure_count_.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(kTag, "CPS notify allocation failed");
    }
}

int BleCyclingPowerService::lastNotifyResult() const { return last_notify_result_.load(std::memory_order_relaxed); }
uint32_t BleCyclingPowerService::notifySuccessCount() const { return notify_success_count_.load(std::memory_order_relaxed); }
uint32_t BleCyclingPowerService::notifyFailureCount() const { return notify_failure_count_.load(std::memory_order_relaxed); }
int16_t BleCyclingPowerService::lastNotifiedPowerWatts() const { return last_notified_power_watts_.load(std::memory_order_relaxed); }
float BleCyclingPowerService::lastNotifiedCadenceRpm() const {
    return static_cast<float>(last_notified_cadence_x100_.load(std::memory_order_relaxed)) / 100.0F;
}
uint16_t BleCyclingPowerService::lastNotifiedCrankRevolutions() const {
    return last_notified_crank_revolutions_.load(std::memory_order_relaxed);
}
uint16_t BleCyclingPowerService::lastNotifiedCrankEventTime() const {
    return last_notified_crank_event_time_.load(std::memory_order_relaxed);
}
int64_t BleCyclingPowerService::lastNotifyUs() const { return last_notify_us_.load(std::memory_order_relaxed); }
bool BleCyclingPowerService::measurementSubscribed() const {
    return measurement_subscribed_.load(std::memory_order_relaxed);
}
uint32_t BleCyclingPowerService::transmitEventCount() const {
    return transmit_event_count_.load(std::memory_order_relaxed);
}
uint32_t BleCyclingPowerService::transmitErrorCount() const {
    return transmit_error_count_.load(std::memory_order_relaxed);
}
int BleCyclingPowerService::lastTransmitStatus() const {
    return last_transmit_status_.load(std::memory_order_relaxed);
}
uint32_t BleCyclingPowerService::disconnectCount() const {
    return disconnect_count_.load(std::memory_order_relaxed);
}
int BleCyclingPowerService::lastDisconnectReason() const {
    return last_disconnect_reason_.load(std::memory_order_relaxed);
}
uint16_t BleCyclingPowerService::connectionIntervalUnits() const {
    return connection_interval_units_.load(std::memory_order_relaxed);
}
int8_t BleCyclingPowerService::connectionRssiDbm() {
    if (!connected_.load(std::memory_order_relaxed)) return -127;
    int8_t rssi = -127;
    if (ble_gap_conn_rssi(conn_handle_.load(std::memory_order_relaxed), &rssi) == 0) {
        connection_rssi_dbm_.store(rssi, std::memory_order_relaxed);
    }
    return connection_rssi_dbm_.load(std::memory_order_relaxed);
}

void BleCyclingPowerService::setDiagnostics(const char *text) {
    if (text == nullptr) {
        return;
    }
    std::strncpy(g_diagnostics, text, sizeof(g_diagnostics) - 1);
    g_diagnostics[sizeof(g_diagnostics) - 1] = '\0';
}

void BleCyclingPowerService::stop() {
    ble_gap_adv_stop();
    if (connected_.load(std::memory_order_relaxed)) {
        ble_gap_terminate(conn_handle_.load(std::memory_order_relaxed), BLE_ERR_REM_USER_CONN_TERM);
    }
    connected_.store(false, std::memory_order_relaxed);
}

void BleCyclingPowerService::resumeAdvertising() {
    advertise();
}

int BleCyclingPowerService::gapEvent(ble_gap_event *event, void *arg) {
    return static_cast<BleCyclingPowerService *>(arg)->onGapEvent(event);
}

int BleCyclingPowerService::gattAccess(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt,
                                       void *arg) {
    (void)conn_handle;
    (void)arg;
    if (g_service == nullptr) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return g_service->onGattAccess(attr_handle, ctxt);
}

void BleCyclingPowerService::onSync() {
    if (ble_hs_id_infer_auto(0, &g_addr_type) == 0 && g_service != nullptr) {
        g_service->advertise();
    }
}

void BleCyclingPowerService::hostTask(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleCyclingPowerService::advertise() {
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t *>(device_name_);
    fields.name_len = std::strlen(device_name_);
    fields.name_is_complete = 1;
    fields.appearance = 0x0484;  // Cycling: Cycling Power Sensor.
    fields.appearance_is_present = 1;
    fields.uuids16 = &service_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    ble_gap_adv_params adv_params{};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(g_addr_type, nullptr, BLE_HS_FOREVER, &adv_params, &BleCyclingPowerService::gapEvent, this);
    ESP_LOGI(kTag, "BLE Cycling Power Service advertising");
}

int BleCyclingPowerService::onGapEvent(ble_gap_event *event) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            connected_.store(event->connect.status == 0, std::memory_order_relaxed);
            if (connected_.load(std::memory_order_relaxed)) {
                conn_handle_.store(event->connect.conn_handle, std::memory_order_relaxed);
                const esp_err_t power_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0,
                                                                 closestPowerLevel(configured_power_dbm_));
                if (power_err != ESP_OK) {
                    ESP_LOGW(kTag, "could not set connection power: %s", esp_err_to_name(power_err));
                }
                ble_gap_conn_desc desc{};
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                    connection_interval_units_.store(desc.conn_itvl, std::memory_order_relaxed);
                }
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            connected_.store(false, std::memory_order_relaxed);
            measurement_subscribed_.store(false, std::memory_order_relaxed);
            disconnect_count_.fetch_add(1, std::memory_order_relaxed);
            last_disconnect_reason_.store(event->disconnect.reason, std::memory_order_relaxed);
            ESP_LOGW(kTag, "BLE disconnected reason=%d", event->disconnect.reason);
            advertise();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == g_measurement_handle) {
                measurement_subscribed_.store(event->subscribe.cur_notify != 0, std::memory_order_relaxed);
                ESP_LOGI(kTag, "CPS subscription notify=%u reason=%u",
                         static_cast<unsigned>(event->subscribe.cur_notify),
                         static_cast<unsigned>(event->subscribe.reason));
            }
            break;
        case BLE_GAP_EVENT_NOTIFY_TX:
            if (event->notify_tx.attr_handle == g_measurement_handle) {
                transmit_event_count_.fetch_add(1, std::memory_order_relaxed);
                last_transmit_status_.store(event->notify_tx.status, std::memory_order_relaxed);
                if (event->notify_tx.status != 0) {
                    transmit_error_count_.fetch_add(1, std::memory_order_relaxed);
                    ESP_LOGW(kTag, "CPS transmit event failed status=%d", event->notify_tx.status);
                }
            }
            break;
        case BLE_GAP_EVENT_CONN_UPDATE: {
            ble_gap_conn_desc desc{};
            if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
                connection_interval_units_.store(desc.conn_itvl, std::memory_order_relaxed);
                ESP_LOGI(kTag, "BLE connection interval updated to %.2f ms",
                         static_cast<double>(desc.conn_itvl) * 1.25);
            }
            break;
        }
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;
        default:
            break;
    }
    return 0;
}

int BleCyclingPowerService::onGattAccess(uint16_t attr_handle, ble_gatt_access_ctxt *ctxt) {
    if (attr_handle == g_feature_handle && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint32_t features = featureBits();
        os_mbuf_append(ctxt->om, &features, sizeof(features));
        return 0;
    }
    if (attr_handle == g_sensor_location_handle && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t left_crank = 5;
        os_mbuf_append(ctxt->om, &left_crank, sizeof(left_crank));
        return 0;
    }
    if (attr_handle == g_diagnostics_handle && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, g_diagnostics, std::strlen(g_diagnostics));
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

}  // namespace openwatts
