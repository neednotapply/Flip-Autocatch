
/**
 * Autocatch main entrypoint – external app.
 * Blink blue on successful catch; blink red once if BLE init fails.
 */
#include <furi.h>
#include <notification/notification_messages.h>
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
    (void)ctx; /* suppress unused‑param warning */

    NotificationApp* n = furi_record_open(RECORD_NOTIFICATION);

    if (!pgp_init()) {
        notification_message_block(n, &sequence_blink_red_100);
        furi_record_close(RECORD_NOTIFICATION);
        return 0;
    }

    run_loop(n);
    furi_record_close(RECORD_NOTIFICATION);
    return 0;
}
