#include "ble_cycling_power.h"

#include <cstdint>
#include <cstring>

#include "esp_log.h"
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

esp_err_t BleCyclingPowerService::begin(const char *device_name) {
    g_service = this;
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
    const uint16_t flags = 0x0020;  // Crank revolution data present.
    payload[0] = static_cast<uint8_t>(flags & 0xFF);
    payload[1] = static_cast<uint8_t>(flags >> 8);
    payload[2] = static_cast<uint8_t>(sample.power_watts & 0xFF);
    payload[3] = static_cast<uint8_t>((static_cast<uint16_t>(sample.power_watts) >> 8) & 0xFF);
    payload[4] = static_cast<uint8_t>(sample.cumulative_crank_revolutions & 0xFF);
    payload[5] = static_cast<uint8_t>(sample.cumulative_crank_revolutions >> 8);
    payload[6] = static_cast<uint8_t>(sample.last_crank_event_time & 0xFF);
    payload[7] = static_cast<uint8_t>(sample.last_crank_event_time >> 8);

    os_mbuf *om = ble_hs_mbuf_from_flat(payload, sizeof(payload));
    if (om != nullptr) {
        ble_gatts_notify_custom(conn_handle_.load(std::memory_order_relaxed), g_measurement_handle, om);
    }
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
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            connected_.store(false, std::memory_order_relaxed);
            advertise();
            break;
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
