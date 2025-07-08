
/**
 * Autocatch main entrypoint – external app.
 * Blink blue on successful catch; blink red once if BLE init fails.
 */
#include <furi.h>
#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>
#include <furi_hal_bt.h>
#include "pgp.h"

static void run_loop(NotificationApp* n) {
    while (pgp_is_connected()) {
        if (pgp_press_requested()) {
            pgp_send_button_press();
            notification_message_block(n, &sequence_blink_blue_100);
        }
        furi_delay_ms(200);
    }
}

int32_t autocatch_app(void* ctx) {
    UNUSED(ctx);

    NotificationApp* n = furi_record_open(RECORD_NOTIFICATION);
    DialogsApp* d = furi_record_open(RECORD_DIALOGS);

    DialogMessage* msg = dialog_message_alloc();
    dialog_message_set_header(msg, "Flip Autocatch", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(msg, "Press OK to pair", 64, 20, AlignCenter, AlignTop);
    dialog_message_set_buttons(msg, "Exit", "Pair", NULL);
    DialogMessageButton res = dialog_message_show(d, msg);
    dialog_message_free(msg);

    if(res == DialogMessageButtonCenter) {
        if(!pgp_init()) {
            notification_message_block(n, &sequence_blink_red_100);
        } else {
            notification_message_block(n, &sequence_success);

            DialogMessage* pair = dialog_message_alloc();
            dialog_message_set_header(pair, "Pairing", 64, 0, AlignCenter, AlignTop);
            dialog_message_set_text(
                pair,
                "Open PoGo >\nAccessory Devices\nand select Pokemon GO Plus",
                64,
                20,
                AlignCenter,
                AlignTop);
            dialog_message_set_buttons(pair, "Back", NULL, NULL);
            dialog_message_show(d, pair);
            dialog_message_free(pair);

            furi_hal_bt_stop_advertising();
            run_loop(n);
        }
    }

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    return 0;
}
