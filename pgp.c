
/**
 * BLE layer for Pokémon Go Plus spoofing – Momentum firmware.
 * Relies on official ble_glue headers now exposed via env.modules.add("lib/ble").
 */
#include "pgp.h"
#include "pgp_device.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_ble/profile_interface.h>
#include <gap.h>
#include <furi_ble/gatt.h>
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

/* Forward declarations */
static void pgp_button_write_cb(FuriHalBleGattChar*, const uint8_t*, uint16_t, void*);
static bool  pgp_gap_event_cb(GapEvent, void*);

/* Character template list (one characteristic) */
static FuriHalBleCharTemplate pgp_char_templates[] = {
    {
        .uuid128 = {0}, /* patched at runtime */
        .props   = BLE_GATT_CHAR_PROP_WRITE_NO_RESP,
        .max_len = 1,
        .handler = pgp_button_write_cb,
    },
};

/* Preferred connection parameters */
static const FuriHalBlePreferredConn pgp_pref_conn = {
    .min_interval = 24,
    .max_interval = 40,
    .latency      = 0,
    .sup_timeout  = 200,
};

/* Profile template – MAC injected in pgp_init() */
static FuriHalBleProfileTemplate pgp_profile_template = {
    .name               = "PGP",
    .chars              = pgp_char_templates,
    .chars_num          = ARRAY_SIZE(pgp_char_templates),
    .preferred_conn     = &pgp_pref_conn,
    .profile_addr_type  = BLE_GAP_ADDR_TYPE_RANDOM_STATIC,
    .profile_static_addr = {0},
};

/* Pointer to runtime GATT char for notify */
static FuriHalBleGattChar* pgp_button_char = NULL;

/* ------------------------------------------------------------------ */
/* Helper accessors                                                   */
bool pgp_is_connected(void)    { return pgp_connected; }
bool pgp_press_requested(void) { return press_requested; }

void pgp_send_button_press(void) {
    static const uint8_t pkt = 0x03; /* press + release */
    if (pgp_button_char) {
        furi_hal_ble_char_notify(pgp_button_char, &pkt, 1);
    }
    press_requested = false;
}

/* Write callback – phone writes 0x01 to request a catch */
static void pgp_button_write_cb(FuriHalBleGattChar* ch,
                                const uint8_t* data,
                                uint16_t len,
                                void* ctx) {
    UNUSED(ch); UNUSED(ctx);
    if (len && data[0] == 0x01) press_requested = true;
}

/* GAP link status */
static bool pgp_gap_event_cb(GapEvent ev, void* ctx) {
    UNUSED(ctx);
    pgp_connected = (ev == GapConnected);
    return true;
}

/* ------------------------------------------------------------------ */
bool pgp_init(void) {
    /* Patch MAC and UUID into template */
    memcpy(pgp_profile_template.profile_static_addr, PGP_MAC, 6);
    memcpy(pgp_char_templates[0].uuid128, PGP_BTN_UUID, 16);

    /* Inject saved bonding blob (optional) */
    if (sizeof(PGP_BLOB)) {
        uint8_t* kv; uint16_t kv_len;
        furi_hal_bt_get_key_storage_buff(&kv, &kv_len);
        if (sizeof(PGP_BLOB) <= kv_len) {
            furi_hal_bt_nvm_sram_sem_acquire();
            memcpy(kv, PGP_BLOB, sizeof(PGP_BLOB));
            furi_hal_bt_nvm_sram_sem_release();
        }
    }

    /* Start radio + BLE profile */
    if (!furi_hal_bt_start_radio_stack()) return false;

    FuriHalBleProfileBase* base =
        furi_hal_bt_start_app(&pgp_profile_template, NULL, pgp_gap_event_cb, NULL);
    if (!base) return false;

    pgp_button_char = &base->chars[0];
    furi_hal_bt_start_advertising();
    return true;
}
