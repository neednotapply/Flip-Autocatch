#include <furi.h>
#include <furi_hal_bt.h>

#include <stdint.h>
#include <stdbool.h>

/*
 * Momentum SDK provides helpers to spoof the BLE identity for Pokemon Go
 * Plus emulation. These functions are part of Momentum firmware but not
 * available in the open source Flipper SDK, so we provide weak stubs to
 * allow building the application without Momentum. When running on
 * Momentum firmware the real implementations will be used instead.
 */

#ifndef MOMENTUM_SDK
/*
 * Build without Momentum: define weak stubs so linking succeeds. When running
 * on Momentum firmware these will be ignored in favour of real implementations.
 */
__attribute__((weak)) void furi_hal_bt_set_mac_address(const uint8_t mac[6]) {
    UNUSED(mac);
}

__attribute__((weak)) void furi_hal_bt_set_device_key(const uint8_t key[16]) {
    UNUSED(key);
}

#include <stddef.h>
__attribute__((weak)) void furi_hal_bt_set_blob(const void* blob, size_t len) {
    UNUSED(blob);
    UNUSED(len);
}

// The Momentum SDK exposes helpers for Pokemon Go Plus emulation.
// When building without it we provide weak stubs so the code still compiles.
__attribute__((weak)) bool pgp_is_connected(void) {
    return false;
}

__attribute__((weak)) bool pgp_press_requested(void) {
    return false;
}

__attribute__((weak)) void pgp_send_button_press(void) {
}
#endif
/* --------------------------------------------------------------------- */

#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>
#include "pgp_device.h"

/* optional image assets would be generated here */

static void autocatch_loop(NotificationApp* notifications) {
    while(pgp_is_connected()) {
        if(pgp_press_requested()) {
            // automatically respond to catch events
            pgp_send_button_press();
            notification_message_block(notifications, &sequence_blink_once);
        }
        furi_delay_ms(200);
    }
}

int32_t autocatch_app(void* p) {
    UNUSED(p);

    // Acquire required services
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);

    // Configure Bluetooth identity for Pokemon Go Plus emulation
    // Momentum firmware provides functions to set the BLE MAC address and keys.
    // These calls are no-ops on vanilla firmware but harmless.
    furi_hal_bt_set_mac_address(PGP_MAC);
    furi_hal_bt_set_device_key(PGP_DEVICE_KEY);
    furi_hal_bt_set_blob(PGP_BLOB, sizeof(PGP_BLOB));

    // Prepare and show pairing dialog
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Flip Autocatch", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, "Press OK to pair", 64, 20, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, "Exit", "Pair", NULL);
    DialogMessageButton result = dialog_message_show(dialogs, message);
    dialog_message_free(message);

    if(result == DialogMessageButtonCenter) {
        // Start BLE advertising to allow pairing
        furi_hal_bt_start_radio_stack();
        furi_hal_bt_start_advertising();
        notification_message_block(notifications, &sequence_success);

        // Show message while advertising so the user can pair
        DialogMessage* pair_msg = dialog_message_alloc();
        dialog_message_set_header(pair_msg, "Pairing", 64, 0, AlignCenter, AlignTop);
        dialog_message_set_text(
            pair_msg,
            "Open PoGo >\nAccessory Devices\nand select Autocatch",
            64,
            20,
            AlignCenter,
            AlignTop);
        dialog_message_set_buttons(pair_msg, "Back", NULL, NULL);
        dialog_message_show(dialogs, pair_msg);
        dialog_message_free(pair_msg);

        // Stop advertising once user leaves the dialog
        furi_hal_bt_stop_advertising();

        // Continue running while connected to Pokemon Go and press the
        // virtual button whenever the game requests it
        autocatch_loop(notifications);
    }

    // Release services
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);

    return 0;
}
