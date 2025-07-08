#include "pgp.h"
#include "pgp_device.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_ble/profile_interface.h>
#include <furi_ble/gatt.h>
#include <gap.h>
#include <string.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* Runtime flags */
static volatile bool pgp_connected = false;
static volatile bool press_requested = false;

/* UUID for the PGP button characteristic */
static const uint8_t PGP_BTN_UUID[16] = {
    0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,
    0x00,0x10,0x00,0x00,0x33,0xFD,0x00,0x00
};

/* ---- New BLE structures -------------------------------------------------- */

/* Characteristic descriptor */
static BleGattCharacteristicDescriptorParams pgp_char_descr = {
    .uuid_type = UUID_TYPE_128,
    .max_length = 1,
    .security_permissions = ATTR_PERMISSION_NONE,
    .access_permissions = ATTR_ACCESS_WRITE_WITHOUT_RESPONSE,
    .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

/* Characteristic parameters */
static BleGattCharacteristicParams pgp_char_params = {
    .name = "button",
    .descriptor_params = &pgp_char_descr,
    .data = {.fixed = {NULL, 0}},
    .uuid_type = UUID_TYPE_128,
    .char_properties = CHAR_PROP_WRITE_WITHOUT_RESP | CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

/* Preferred connection parameters */
static const GapConnectionParamsRequest pgp_pref_conn = {
    .conn_int_min = 24,
    .conn_int_max = 40,
    .slave_latency = 0,
    .supervisor_timeout = 200,
};

/* BLE profile instance */
typedef struct {
    FuriHalBleProfileBase base;
    uint16_t svc_handle;
    BleGattCharacteristicInstance button_char;
} PgpProfile;

static PgpProfile pgp_profile;

/* ------------------------------------------------------------------------- */
static bool pgp_gap_event_cb(GapEvent ev, void* ctx) {
    UNUSED(ctx);
    pgp_connected = (ev.type == GapEventTypeConnected);
    return true;
}

static void pgp_get_gap_config(GapConfig* cfg, FuriHalBleProfileParams params) {
    UNUSED(params);
    memset(cfg, 0, sizeof(GapConfig));
    memcpy(cfg->mac_address, PGP_MAC, 6);
    memcpy(cfg->adv_name, "PGP", 3);
    cfg->bonding_mode = true;
    cfg->conn_param = pgp_pref_conn;
}

static FuriHalBleProfileBase* pgp_start(FuriHalBleProfileParams params) {
    UNUSED(params);
    Service_UUID_t svc_uuid = {.Service_UUID_16 = 0xFD00};
    memcpy(pgp_char_descr.uuid.Char_UUID_128, PGP_BTN_UUID, 16);
    memcpy(pgp_char_params.uuid.Char_UUID_128, PGP_BTN_UUID, 16);

    if(!ble_gatt_service_add(UUID_TYPE_16, &svc_uuid, PRIMARY_SERVICE, 2, &pgp_profile.svc_handle)) {
        return NULL;
    }

    ble_gatt_characteristic_init(pgp_profile.svc_handle, &pgp_char_params, &pgp_profile.button_char);
    return &pgp_profile.base;
}

static void pgp_stop(FuriHalBleProfileBase* base) {
    (void)base;
    ble_gatt_characteristic_delete(pgp_profile.svc_handle, &pgp_profile.button_char);
    ble_gatt_service_delete(pgp_profile.svc_handle);
}

static const FuriHalBleProfileTemplate pgp_profile_template = {
    .start = pgp_start,
    .stop = pgp_stop,
    .get_gap_config = pgp_get_gap_config,
};

/* ------------------------------------------------------------------ */
bool pgp_is_connected(void) { return pgp_connected; }
bool pgp_press_requested(void) { return press_requested; }

void pgp_send_button_press(void) {
    static const uint8_t pkt = 0x03; /* press + release */
    ble_gatt_characteristic_update(pgp_profile.svc_handle, &pgp_profile.button_char, &pkt);
    press_requested = false;
}

/* ------------------------------------------------------------------ */
bool pgp_init(void) {
    if(!furi_hal_bt_start_radio_stack()) return false;
    if(!furi_hal_bt_start_app(&pgp_profile_template, NULL, pgp_gap_event_cb, NULL)) return false;
    furi_hal_bt_start_advertising();
    return true;
}
