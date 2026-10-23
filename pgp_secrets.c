#include "pgp_secrets.h"

#include <furi.h>
#include <flipper_format/flipper_format.h>

#define TAG "PgpSecrets"

#define PGP_SECRETS_FILETYPE "Flip Autocatch PGP"
#define PGP_SECRETS_VERSION  1

PgpSecretsStatus pgp_secrets_load(PgpSecrets* secrets) {
    PgpSecretsStatus status = PgpSecretsStatusInvalid;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    uint32_t version = 0;

    memset(secrets, 0, sizeof(PgpSecrets));

    do {
        if(!storage_file_exists(storage, PGP_SECRETS_PATH)) {
            status = PgpSecretsStatusMissing;
            break;
        }
        if(!flipper_format_file_open_existing(file, PGP_SECRETS_PATH)) break;
        if(!flipper_format_read_header(file, str, &version)) break;
        if(furi_string_cmp_str(str, PGP_SECRETS_FILETYPE) || version != PGP_SECRETS_VERSION) {
            FURI_LOG_E(TAG, "Unsupported file type or version");
            break;
        }

        // Name is optional
        if(flipper_format_read_string(file, "Name", str)) {
            strlcpy(secrets->name, furi_string_get_cstr(str), sizeof(secrets->name));
        } else {
            strlcpy(secrets->name, "Pokemon GO Plus", sizeof(secrets->name));
            flipper_format_rewind(file);
        }

        if(!flipper_format_read_hex(file, "MAC", secrets->mac, sizeof(secrets->mac))) {
            FURI_LOG_E(TAG, "Missing or bad MAC");
            break;
        }
        if(!flipper_format_read_hex(
               file, "Device Key", secrets->device_key, sizeof(secrets->device_key))) {
            FURI_LOG_E(TAG, "Missing or bad Device Key");
            break;
        }
        if(!flipper_format_read_hex(file, "Blob", secrets->blob, sizeof(secrets->blob))) {
            FURI_LOG_E(TAG, "Missing or bad Blob");
            break;
        }

        status = PgpSecretsStatusOk;
    } while(false);

    furi_string_free(str);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    return status;
}
