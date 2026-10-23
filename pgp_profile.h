/**
 * BLE profile that looks like a Pokemon GO Plus: battery service, LED/button
 * service and certificate (handshake) service, advertised as "Pokemon GO Plus"
 * with the MAC address of the dumped device.
 */
#pragma once

#include <furi_ble/profile_interface.h>

#include <stdbool.h>
#include <stdint.h>

#define PGP_GATT_EVENT_DATA_MAX (128)

typedef enum {
    PgpGattEventCertNotify, // phone wrote SFIDA_COMMANDS CCCD, data[0] = enabled
    PgpGattEventCertWrite, // phone wrote CENTRAL_TO_SFIDA
    PgpGattEventLedWrite, // phone wrote an LED/vibration pattern
} PgpGattEventType;

typedef struct {
    PgpGattEventType type;
    uint16_t len;
    uint8_t data[PGP_GATT_EVENT_DATA_MAX];
} PgpGattEvent;

/* Called from the BLE event thread: copy the event and return quickly */
typedef void (*PgpGattEventCallback)(const PgpGattEvent* event, void* context);

typedef struct {
    const uint8_t* mac; // display order, e.g. 98:B6:E9:..
    PgpGattEventCallback callback;
    void* context;
} BleProfilePgpParams;

extern const FuriHalBleProfileTemplate* ble_profile_pgp;

/** Publish a value on SFIDA_TO_CENTRAL (up to 378 bytes) for the phone to read */
bool ble_profile_pgp_set_cert(FuriHalBleProfileBase* profile, uint8_t* data, uint16_t len);

/** Notify 4 bytes on SFIDA_COMMANDS */
void ble_profile_pgp_send_command(FuriHalBleProfileBase* profile, const uint8_t* command);

/** Notify the 2 byte button sample pattern */
void ble_profile_pgp_send_button(FuriHalBleProfileBase* profile, const uint8_t* pattern);
