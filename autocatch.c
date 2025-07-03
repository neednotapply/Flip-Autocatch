#include <furi.h>
#include <furi_hal_bt.h>
#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>

/* optional image assets would be generated here */

int32_t autocatch_app(void* p) {
    UNUSED(p);

    // Acquire required services
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);

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
    }

    // Release services
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);

    return 0;
}
