/**
 * Device secrets (MAC, device key, blob) dumped from your own Pokemon GO Plus,
 * loaded from the SD card so they never end up in the source tree or the .fap.
 */
#pragma once

#include "pgp_cert.h"

#include <storage/storage.h>

#define PGP_SECRETS_PATH         APP_DATA_PATH("pgp_device.txt")
#define PGP_SECRETS_DISPLAY_PATH "apps_data/autocatch/pgp_device.txt"
#define PGP_SECRETS_NAME_LEN     (24)

typedef struct {
    char name[PGP_SECRETS_NAME_LEN];
    uint8_t mac[PGP_MAC_LEN]; // display order, e.g. 98:B6:E9:..
    uint8_t device_key[PGP_KEY_LEN];
    uint8_t blob[PGP_BLOB_LEN];
} PgpSecrets;

typedef enum {
    PgpSecretsStatusOk,
    PgpSecretsStatusMissing,
    PgpSecretsStatusInvalid,
} PgpSecretsStatus;

PgpSecretsStatus pgp_secrets_load(PgpSecrets* secrets);
