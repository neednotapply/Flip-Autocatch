#include "pgp_profile.h"

#include <furi.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <services/battery_service.h>
#include <services/serial_service.h>

#include <string.h>

#define TAG "PgpProfile"

#define PGP_ADV_NAME "Pokemon GO Plus"

#define PGP_CERT_VALUE_LEN_MAX (378)
#define PGP_LED_VALUE_LEN_MAX  (100)
#define PGP_MISC_VALUE_LEN_MAX (20)

/* The HCI transport headers (tl.h, ble_vs_codes.h) aren't shipped in the SDK */
#ifndef ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE
#define ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE (0x0C01U)
#endif

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t data[];
} PgpHciUartPacket;

typedef struct __attribute__((packed)) {
    uint8_t evt;
    uint8_t plen;
    uint8_t data[];
} PgpHciEventPacket;

typedef struct __attribute__((packed)) {
    uint16_t ecode;
    uint8_t data[];
} PgpBlecoreEvent;

/* 128-bit UUIDs, least significant byte first */
#define PGP_LED_BUTTON_UUID(b0) \
    {b0, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x04, 0xc5, 0x21}
#define PGP_CERT_UUID(b0) \
    {b0, 0x8e, 0x0d, 0xef, 0x8e, 0x8b, 0x7f, 0xab, 0x33, 0x44, 0x89, 0x5b, 0x09, 0x77, 0xe8, 0xbb}

static const uint8_t pgp_led_button_svc_uuid[16] = PGP_LED_BUTTON_UUID(0xeb);
static const uint8_t pgp_cert_svc_uuid[16] = PGP_CERT_UUID(0x37);

typedef enum {
    PgpLedButtonCharLed,
    PgpLedButtonCharButton,
    PgpLedButtonCharUnknown,
    PgpLedButtonCharUpdateRequest,
    PgpLedButtonCharFwVersion,
    PgpLedButtonCharCount,
} PgpLedButtonChar;

typedef enum {
    PgpCertCharCentralToSfida,
    PgpCertCharSfidaCommands,
    PgpCertCharSfidaToCentral,
    PgpCertCharCount,
} PgpCertChar;

static const BleGattCharacteristicParams pgp_led_button_chars[PgpLedButtonCharCount] = {
    [PgpLedButtonCharLed] =
        {.name = "LED",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = PGP_LED_VALUE_LEN_MAX,
         .uuid.Char_UUID_128 = PGP_LED_BUTTON_UUID(0xec),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_WRITE,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
    [PgpLedButtonCharButton] =
        {.name = "Button",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = 2,
         .uuid.Char_UUID_128 = PGP_LED_BUTTON_UUID(0xed),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_NOTIFY,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_CONSTANT},
    [PgpLedButtonCharUnknown] =
        {.name = "Unknown",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = PGP_MISC_VALUE_LEN_MAX,
         .uuid.Char_UUID_128 = PGP_LED_BUTTON_UUID(0xee),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_WRITE,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
    [PgpLedButtonCharUpdateRequest] =
        {.name = "Update request",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = PGP_MISC_VALUE_LEN_MAX,
         .uuid.Char_UUID_128 = PGP_LED_BUTTON_UUID(0xef),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_WRITE,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
    [PgpLedButtonCharFwVersion] =
        {.name = "FW version",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = PGP_MISC_VALUE_LEN_MAX,
         .uuid.Char_UUID_128 = PGP_LED_BUTTON_UUID(0xf0),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_READ,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
};

static const BleGattCharacteristicParams pgp_cert_chars[PgpCertCharCount] = {
    [PgpCertCharCentralToSfida] =
        {.name = "Central to SFIDA",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = PGP_GATT_EVENT_DATA_MAX,
         .uuid.Char_UUID_128 = PGP_CERT_UUID(0x38),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_WRITE,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
    [PgpCertCharSfidaCommands] =
        {.name = "SFIDA commands",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = 4,
         .uuid.Char_UUID_128 = PGP_CERT_UUID(0x39),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_NOTIFY,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
         .is_variable = CHAR_VALUE_LEN_CONSTANT},
    [PgpCertCharSfidaToCentral] =
        {.name = "SFIDA to central",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = PGP_CERT_VALUE_LEN_MAX,
         .uuid.Char_UUID_128 = PGP_CERT_UUID(0x3a),
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_READ,
         .security_permissions = ATTR_PERMISSION_NONE,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
};

static const GapConfig template_config = {
    // A real PGP lists 0x00FF in its scan response; Flipper's GAP can only put
    // one 16 or 128 bit service UUID next to the name in the advertisement
    .adv_service =
        {
            .UUID_Type = UUID_TYPE_16,
            .Service_UUID_16 = 0x00FF,
        },
    .bonding_mode = true,
    .pairing_method = GapPairingPinCodeVerifyYesNo,
    .conn_param =
        {
            .conn_int_min = 0x18, // 30 ms
            .conn_int_max = 0x24, // 45 ms
            .slave_latency = 0,
            .supervisor_timeout = 0,
        },
};

_Static_assert(
    sizeof(PGP_ADV_NAME) + 1 <= sizeof(template_config.adv_name),
    "Advertising name too long");

typedef struct {
    FuriHalBleProfileBase base;

    BleServiceBattery* battery_svc;
    uint16_t led_button_svc_handle;
    BleGattCharacteristicInstance led_button_chars[PgpLedButtonCharCount];
    uint16_t cert_svc_handle;
    BleGattCharacteristicInstance cert_chars[PgpCertCharCount];

    GapSvcEventHandler* event_handler;
    PgpGattEventCallback callback;
    void* context;

    // Long (prepared) writes may be reported in several events
    uint16_t write_handle;
    uint8_t write_buf[PGP_GATT_EVENT_DATA_MAX];
} BleProfilePgp;

static bool ble_profile_pgp_collect_write(
    BleProfilePgp* pgp,
    const aci_gatt_attribute_modified_event_rp0* attribute_modified,
    PgpGattEvent* gatt_event) {
    // Bits 14-0 are the offset, bit 15 is set while more data follows
    uint16_t offset = attribute_modified->Offset & 0x7fff;
    bool more = attribute_modified->Offset & 0x8000;
    uint16_t len = attribute_modified->Attr_Data_Length;

    if(offset == 0) {
        pgp->write_handle = attribute_modified->Attr_Handle;
    } else if(pgp->write_handle != attribute_modified->Attr_Handle) {
        return false;
    }

    if(offset + len > sizeof(pgp->write_buf)) {
        FURI_LOG_W(TAG, "Write too long: %u bytes at offset %u", len, offset);
        pgp->write_handle = 0;
        return false;
    }

    memcpy(pgp->write_buf + offset, attribute_modified->Attr_Data, len);
    if(more) {
        return false;
    }

    gatt_event->len = offset + len;
    memcpy(gatt_event->data, pgp->write_buf, gatt_event->len);
    return true;
}

static BleEventAckStatus ble_profile_pgp_event_handler(void* event, void* context) {
    BleProfilePgp* pgp = context;
    PgpHciEventPacket* event_pckt = (PgpHciEventPacket*)((PgpHciUartPacket*)event)->data;
    if(event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        return BleEventNotAck;
    }

    PgpBlecoreEvent* blecore_evt = (PgpBlecoreEvent*)event_pckt->data;
    if(blecore_evt->ecode != ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
        return BleEventNotAck;
    }

    aci_gatt_attribute_modified_event_rp0* attribute_modified =
        (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;
    uint16_t handle = attribute_modified->Attr_Handle;
    PgpGattEvent gatt_event;

    // Value handle is declaration handle + 1, CCCD is + 2
    if(handle == pgp->cert_chars[PgpCertCharSfidaCommands].handle + 2) {
        gatt_event.type = PgpGattEventCertNotify;
        gatt_event.len = 1;
        gatt_event.data[0] = attribute_modified->Attr_Data_Length > 0 &&
                             (attribute_modified->Attr_Data[0] & 0x01);
    } else if(handle == pgp->cert_chars[PgpCertCharCentralToSfida].handle + 1) {
        gatt_event.type = PgpGattEventCertWrite;
        if(!ble_profile_pgp_collect_write(pgp, attribute_modified, &gatt_event)) {
            return BleEventAckFlowEnable;
        }
    } else if(handle == pgp->led_button_chars[PgpLedButtonCharLed].handle + 1) {
        gatt_event.type = PgpGattEventLedWrite;
        if(!ble_profile_pgp_collect_write(pgp, attribute_modified, &gatt_event)) {
            return BleEventAckFlowEnable;
        }
    } else {
        return BleEventNotAck;
    }

    if(pgp->callback) {
        pgp->callback(&gatt_event, pgp->context);
    }
    return BleEventAckFlowEnable;
}

static bool ble_profile_pgp_add_service(
    const uint8_t* uuid,
    const BleGattCharacteristicParams* chars,
    size_t char_count,
    uint16_t* svc_handle,
    BleGattCharacteristicInstance* char_instances) {
    Service_UUID_t svc_uuid;
    memcpy(svc_uuid.Service_UUID_128, uuid, sizeof(svc_uuid.Service_UUID_128));

    // Service declaration + declaration and value per characteristic + one CCCD
    uint8_t max_records = 1 + 2 * char_count + 1;
    if(!ble_gatt_service_add(UUID_TYPE_128, &svc_uuid, PRIMARY_SERVICE, max_records, svc_handle)) {
        return false;
    }
    for(size_t i = 0; i < char_count; i++) {
        ble_gatt_characteristic_init(*svc_handle, &chars[i], &char_instances[i]);
    }
    return true;
}

static void ble_profile_pgp_delete_service(
    uint16_t svc_handle,
    BleGattCharacteristicInstance* char_instances,
    size_t char_count) {
    if(!svc_handle) {
        return;
    }
    for(size_t i = 0; i < char_count; i++) {
        ble_gatt_characteristic_delete(svc_handle, &char_instances[i]);
    }
    ble_gatt_service_delete(svc_handle);
}

static void ble_profile_pgp_stop(FuriHalBleProfileBase* profile) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_pgp);

    BleProfilePgp* pgp = (BleProfilePgp*)profile;
    if(pgp->event_handler) {
        ble_event_dispatcher_unregister_svc_handler(pgp->event_handler);
    }
    ble_profile_pgp_delete_service(pgp->cert_svc_handle, pgp->cert_chars, PgpCertCharCount);
    ble_profile_pgp_delete_service(
        pgp->led_button_svc_handle, pgp->led_button_chars, PgpLedButtonCharCount);
    ble_svc_battery_stop(pgp->battery_svc);
    free(pgp);
}

static FuriHalBleProfileBase* ble_profile_pgp_start(FuriHalBleProfileParams profile_params) {
    BleProfilePgpParams* params = profile_params;
    furi_check(params);

    BleProfilePgp* pgp = malloc(sizeof(BleProfilePgp));
    memset(pgp, 0, sizeof(BleProfilePgp));
    pgp->base.config = ble_profile_pgp;
    pgp->callback = params->callback;
    pgp->context = params->context;

    pgp->battery_svc = ble_svc_battery_start(true);

    if(!ble_profile_pgp_add_service(
           pgp_led_button_svc_uuid,
           pgp_led_button_chars,
           PgpLedButtonCharCount,
           &pgp->led_button_svc_handle,
           pgp->led_button_chars) ||
       !ble_profile_pgp_add_service(
           pgp_cert_svc_uuid,
           pgp_cert_chars,
           PgpCertCharCount,
           &pgp->cert_svc_handle,
           pgp->cert_chars)) {
        FURI_LOG_E(TAG, "Failed to add services");
        ble_profile_pgp_stop(&pgp->base);
        return NULL;
    }

    pgp->event_handler = ble_event_dispatcher_register_svc_handler(ble_profile_pgp_event_handler, pgp);
    return &pgp->base;
}

static void ble_profile_pgp_get_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    BleProfilePgpParams* params = profile_params;
    furi_check(config);
    furi_check(params);

    memcpy(config, &template_config, sizeof(GapConfig));

    // GAP expects the address least significant byte first
    for(size_t i = 0; i < GAP_MAC_ADDR_SIZE; i++) {
        config->mac_address[i] = params->mac[GAP_MAC_ADDR_SIZE - 1 - i];
    }

    // First byte of adv_name is the AD type
    memset(config->adv_name, 0, sizeof(config->adv_name));
    config->adv_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(config->adv_name + 1, PGP_ADV_NAME, strlen(PGP_ADV_NAME));
}

static const FuriHalBleProfileTemplate profile_callbacks = {
    .start = ble_profile_pgp_start,
    .stop = ble_profile_pgp_stop,
    .get_gap_config = ble_profile_pgp_get_config,
};

const FuriHalBleProfileTemplate* ble_profile_pgp = &profile_callbacks;

/* ble_gatt_characteristic_update() goes through aci_gatt_update_char_value(),
 * whose length is a uint8_t, so it can't publish the 378 byte first challenge.
 * The only exported call using aci_gatt_update_char_value_ext() is the serial
 * service TX update, and it reads nothing from BleServiceSerial but the service
 * handle and the TX characteristic. Hand it a struct with the same leading
 * layout that points at our characteristic instead. */
typedef struct {
    uint16_t svc_handle;
    BleGattCharacteristicInstance chars[2]; // RX (unused), TX
} PgpSerialServiceShim;

bool ble_profile_pgp_set_cert(FuriHalBleProfileBase* profile, uint8_t* data, uint16_t len) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_pgp);
    furi_check(len <= PGP_CERT_VALUE_LEN_MAX);

    BleProfilePgp* pgp = (BleProfilePgp*)profile;
    PgpSerialServiceShim shim = {.svc_handle = pgp->cert_svc_handle};
    shim.chars[1] = pgp->cert_chars[PgpCertCharSfidaToCentral];
    return ble_svc_serial_update_tx((BleServiceSerial*)&shim, data, len);
}

void ble_profile_pgp_send_command(FuriHalBleProfileBase* profile, const uint8_t* command) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_pgp);

    BleProfilePgp* pgp = (BleProfilePgp*)profile;
    ble_gatt_characteristic_update(
        pgp->cert_svc_handle, &pgp->cert_chars[PgpCertCharSfidaCommands], command);
}

void ble_profile_pgp_send_button(FuriHalBleProfileBase* profile, const uint8_t* pattern) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_pgp);

    BleProfilePgp* pgp = (BleProfilePgp*)profile;
    ble_gatt_characteristic_update(
        pgp->led_button_svc_handle, &pgp->led_button_chars[PgpLedButtonCharButton], pattern);
}
