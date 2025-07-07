#include <furi.h>
#include <furi_hal_bt.h>

#include <stdint.h>

/* ---------------------------------------------------------------------
 * The application relies on additional Bluetooth helpers provided by the
 * Momentum SDK.  Building against the stock Flipper firmware would leave
 * these symbols undefined and the spoofing logic would be ineffective.
 * Fail the build early if the Momentum headers are not in use.
 * ------------------------------------------------------------------- */
#ifndef MOMENTUM_SDK
#error "Momentum SDK required.  Run 'ufbt update --index-url https://up.momentum-fw.dev/firmware/directory.json --channel dev' first"
#endif

#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>
#include "pgp.h"
#include "pgp_device.h"

/* ---------------------------------------------------------------------
 * Main loop: automatically press the PGP button whenever the phone asks.
 * ------------------------------------------------------------------- */
static void autocatch_loop(NotificationApp* notifications) {
    while(pgp_is_connected()) {
        if(pgp_press_requested()) {
            pgp_send_button_press();
            notification_message_block(notifications, &sequence_blink_blue_100);
        }
        furi_delay_ms(200);
    }
}

int32_t autocatch_app(void* p) {
    UNUSED(p);

    /* Acquire system services */
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);

    /* Spoof the original Pokémon Go Plus identity (Momentum only) */
    furi_hal_bt_set_mac_address(PGP_MAC);
    furi_hal_bt_set_device_key(PGP_DEVICE_KEY);
    furi_hal_bt_set_blob(PGP_BLOB, sizeof(PGP_BLOB));

    /* Prepare and show pairing dialog */
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Flip Autocatch", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, "Press OK to pair", 64, 20, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, "Exit", "Pair", NULL);
    DialogMessageButton result = dialog_message_show(dialogs, message);
    dialog_message_free(message);

    if(result == DialogMessageButtonCenter) {
        /* Start BLE advertising to allow pairing */
        furi_hal_bt_start_radio_stack();
        furi_hal_bt_start_advertising();
        notification_message_block(notifications, &sequence_success);

        /* Show message while advertising so the user can pair */
        DialogMessage* pair_msg = dialog_message_alloc();
        dialog_message_set_header(pair_msg, "Pairing", 64, 0, AlignCenter, AlignTop);
        dialog_message_set_text(
            pair_msg,
            "Open PoGo >\nAccessory Devices\nand select Pokemon GO Plus",
            64,
            20,
            AlignCenter,
            AlignTop);
        dialog_message_set_buttons(pair_msg, "Back", NULL, NULL);
        dialog_message_show(dialogs, pair_msg);
        dialog_message_free(pair_msg);

        /* Stop advertising once user leaves the dialog */
        furi_hal_bt_stop_advertising();

        /* Continue running while connected to Pokemon Go and press the
         * virtual button whenever the game requests it */
        autocatch_loop(notifications);
    }

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    return 0;
}
