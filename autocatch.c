/**
 * Flip Autocatch: emulate a Pokemon GO Plus and press its button automatically
 * when the phone shows a Pokemon or Pokestop.
 */
#include "pgp_handshake.h"
#include "pgp_led.h"
#include "pgp_profile.h"
#include "pgp_secrets.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_random.h>
#include <bt/bt_service/bt.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define TAG "Autocatch"

#define AUTOCATCH_KEYS_PATH APP_DATA_PATH(".pgp.keys")

// pgpemu presses 1.0-2.5 s after the LED pattern starts
#define AUTOCATCH_PRESS_DELAY_MIN_MS    1000
#define AUTOCATCH_PRESS_DELAY_RANGE_MS  1500
#define AUTOCATCH_EVENT_QUEUE_SIZE      16
#define AUTOCATCH_LOOP_TIMEOUT_MS       100

typedef enum {
    AutocatchEventTypeInput,
    AutocatchEventTypeBtStatus,
    AutocatchEventTypeGatt,
} AutocatchEventType;

typedef struct {
    AutocatchEventType type;
    union {
        InputEvent input;
        BtStatus bt_status;
        PgpGattEvent gatt;
    };
} AutocatchEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    NotificationApp* notifications;
    FuriMessageQueue* event_queue;
    FuriMutex* mutex;

    Bt* bt;
    FuriHalBleProfileBase* profile;
    BleProfilePgpParams profile_params;

    PgpSecrets secrets;
    PgpSecretsStatus secrets_status;
    PgpHandshake handshake;

    BtStatus bt_status;
    bool handshake_started;
    bool autocatch;
    bool autospin;
    bool press_pending;
    uint32_t press_at;

    PgpLedEvent last_event;
    uint32_t seen;
    uint32_t caught;
    uint32_t fled;
    uint32_t spins;
    uint32_t presses;
} AutocatchApp;

static void autocatch_random_fill(uint8_t* buffer, size_t len) {
    furi_hal_random_fill_buf(buffer, len);
}

static const char* autocatch_status_text(AutocatchApp* app) {
    if(!app->profile) {
        return "Bluetooth start failed";
    }
    if(pgp_handshake_is_done(&app->handshake)) {
        return "Connected";
    }
    // Pairing (and so BtStatusConnected) may finish after the handshake started
    if(app->handshake_started || app->bt_status == BtStatusConnected) {
        switch(app->handshake.state) {
        case PgpHandshakeStateChallenge:
            return "Handshake 1/3";
        case PgpHandshakeStateNextChallenge:
            return "Handshake 2/3";
        default:
            return "Handshake 3/3";
        }
    }
    if(app->bt_status == BtStatusAdvertising) {
        return "Waiting for phone...";
    }
    return "Bluetooth off";
}

static void autocatch_draw_callback(Canvas* canvas, void* context) {
    AutocatchApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(app->secrets_status != PgpSecretsStatusOk) {
        canvas_draw_str(canvas, 0, 10, "PoGo Autocatch");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(
            canvas,
            0,
            22,
            app->secrets_status == PgpSecretsStatusMissing ? "No PGP secrets found." :
                                                             "PGP secrets file invalid.");
        canvas_draw_str(canvas, 0, 34, "Copy your device dump to");
        canvas_draw_str(canvas, 0, 44, PGP_SECRETS_DISPLAY_PATH);
        canvas_draw_str(canvas, 0, 56, "See README for the format.");
    } else {
        FuriString* line = furi_string_alloc();

        canvas_draw_str(canvas, 0, 10, app->secrets.name);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 20, autocatch_status_text(app));
        canvas_draw_str(canvas, 0, 30, pgp_led_event_name(app->last_event));
        furi_string_printf(
            line, "Seen %lu  Caught %lu  Fled %lu", app->seen, app->caught, app->fled);
        canvas_draw_str(canvas, 0, 40, furi_string_get_cstr(line));
        furi_string_printf(line, "Spins %lu  Presses %lu", app->spins, app->presses);
        canvas_draw_str(canvas, 0, 49, furi_string_get_cstr(line));

        // OK presses the button by hand; three button labels don't fit
        furi_string_printf(line, "Catch:%s", app->autocatch ? "on" : "off");
        elements_button_left(canvas, furi_string_get_cstr(line));
        furi_string_printf(line, "Spin:%s", app->autospin ? "on" : "off");
        elements_button_right(canvas, furi_string_get_cstr(line));

        furi_string_free(line);
    }

    furi_mutex_release(app->mutex);
}

static void autocatch_input_callback(InputEvent* input_event, void* context) {
    AutocatchApp* app = context;
    AutocatchEvent event = {.type = AutocatchEventTypeInput, .input = *input_event};
    furi_message_queue_put(app->event_queue, &event, FuriWaitForever);
}

static void autocatch_bt_status_callback(BtStatus status, void* context) {
    AutocatchApp* app = context;
    AutocatchEvent event = {.type = AutocatchEventTypeBtStatus, .bt_status = status};
    furi_message_queue_put(app->event_queue, &event, FuriWaitForever);
}

/* Runs on the BLE event thread: just hand the event over */
static void autocatch_gatt_callback(const PgpGattEvent* gatt_event, void* context) {
    AutocatchApp* app = context;
    AutocatchEvent event = {.type = AutocatchEventTypeGatt, .gatt = *gatt_event};
    if(furi_message_queue_put(app->event_queue, &event, 0) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Event queue full, dropped GATT event %d", gatt_event->type);
    }
}

static void autocatch_press_button(AutocatchApp* app) {
    uint8_t pattern[2];
    pgp_led_button_pattern(furi_hal_random_get(), pattern);
    ble_profile_pgp_send_button(app->profile, pattern);
    app->presses++;
    notification_message(app->notifications, &sequence_blink_blue_10);
}

static void autocatch_send_handshake(AutocatchApp* app, const PgpHandshakeOutput* output) {
    if(output->cert_len &&
       !ble_profile_pgp_set_cert(app->profile, app->handshake.cert_buffer, output->cert_len)) {
        FURI_LOG_E(TAG, "Failed to publish %u byte certificate", output->cert_len);
    }
    if(output->notify) {
        ble_profile_pgp_send_command(app->profile, output->command);
    }
    if(pgp_handshake_is_done(&app->handshake)) {
        FURI_LOG_I(TAG, "Handshake complete");
        notification_message(app->notifications, &sequence_blink_green_100);
    }
}

static void autocatch_handle_led(AutocatchApp* app, const uint8_t* data, size_t len) {
    PgpLedPattern pattern;
    if(!pgp_led_parse(data, len, &pattern)) {
        FURI_LOG_W(TAG, "Bad LED pattern, %u bytes", len);
        return;
    }
    FURI_LOG_I(
        TAG, "LED: %s, %lu ms", pgp_led_event_name(pattern.event), pattern.duration_ms);
    if(pattern.event == PgpLedEventNone) {
        return;
    }
    app->last_event = pattern.event;

    bool press = false;
    switch(pattern.event) {
    case PgpLedEventPokemon:
    case PgpLedEventNewPokemon:
        app->seen++;
        press = app->autocatch;
        break;
    case PgpLedEventPokestop:
        press = app->autospin;
        break;
    case PgpLedEventCaught:
        app->caught++;
        break;
    case PgpLedEventFled:
        app->fled++;
        break;
    case PgpLedEventItems:
        app->spins++;
        break;
    case PgpLedEventUnknown:
        press = app->autocatch || app->autospin;
        break;
    default:
        break;
    }

    if(press) {
        // The press only counts while the pattern is still showing
        uint32_t delay_ms = AUTOCATCH_PRESS_DELAY_MIN_MS +
                            furi_hal_random_get() % (AUTOCATCH_PRESS_DELAY_RANGE_MS + 1);
        if(delay_ms < pattern.duration_ms) {
            app->press_pending = true;
            app->press_at = furi_get_tick() + furi_ms_to_ticks(delay_ms);
        }
    }
}

static void autocatch_handle_gatt(AutocatchApp* app, const PgpGattEvent* event) {
    PgpHandshakeOutput output;
    switch(event->type) {
    case PgpGattEventCertNotify:
        app->handshake_started = event->data[0];
        if(pgp_handshake_on_notify(&app->handshake, event->data[0], &output)) {
            autocatch_send_handshake(app, &output);
        }
        break;
    case PgpGattEventCertWrite:
        if(pgp_handshake_on_write(&app->handshake, event->data, event->len, &output)) {
            autocatch_send_handshake(app, &output);
        } else {
            FURI_LOG_W(
                TAG,
                "Unexpected %u byte write in handshake state %d",
                event->len,
                app->handshake.state);
        }
        break;
    case PgpGattEventLedWrite:
        autocatch_handle_led(app, event->data, event->len);
        break;
    }
}

static bool autocatch_handle_input(AutocatchApp* app, const InputEvent* input) {
    if(input->type != InputTypeShort) {
        return true;
    }
    switch(input->key) {
    case InputKeyBack:
        return false;
    case InputKeyLeft:
        app->autocatch = !app->autocatch;
        break;
    case InputKeyRight:
        app->autospin = !app->autospin;
        break;
    case InputKeyOk:
        if(app->profile && pgp_handshake_is_done(&app->handshake)) {
            autocatch_press_button(app);
        }
        break;
    default:
        break;
    }
    return true;
}

static void autocatch_bt_start(AutocatchApp* app) {
    app->bt = furi_record_open(RECORD_BT);
    bt_disconnect(app->bt);
    // Wait for the 2nd core to update its NVM storage
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app->bt, AUTOCATCH_KEYS_PATH);

    app->profile_params.mac = app->secrets.mac;
    app->profile_params.callback = autocatch_gatt_callback;
    app->profile_params.context = app;
    app->profile = bt_profile_start(app->bt, ble_profile_pgp, &app->profile_params);
    if(!app->profile) {
        FURI_LOG_E(TAG, "Failed to start PGP profile");
        return;
    }

    bt_set_status_changed_callback(app->bt, autocatch_bt_status_callback, app);
    furi_hal_bt_start_advertising();
}

static void autocatch_bt_stop(AutocatchApp* app) {
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));
    furi_record_close(RECORD_BT);
}

static AutocatchApp* autocatch_alloc(void) {
    AutocatchApp* app = malloc(sizeof(AutocatchApp));
    memset(app, 0, sizeof(AutocatchApp));

    app->autocatch = true;
    app->autospin = true;
    app->event_queue = furi_message_queue_alloc(AUTOCATCH_EVENT_QUEUE_SIZE, sizeof(AutocatchEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, autocatch_draw_callback, app);
    view_port_input_callback_set(app->view_port, autocatch_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    return app;
}

static void autocatch_free(AutocatchApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->event_queue);
    free(app);
}

int32_t autocatch_app(void* p) {
    UNUSED(p);
    AutocatchApp* app = autocatch_alloc();

    app->secrets_status = pgp_secrets_load(&app->secrets);
    if(app->secrets_status == PgpSecretsStatusOk) {
        pgp_handshake_init(
            &app->handshake,
            app->secrets.mac,
            app->secrets.device_key,
            app->secrets.blob,
            autocatch_random_fill);
        autocatch_bt_start(app);
    }
    view_port_update(app->view_port);

    AutocatchEvent event;
    bool running = true;
    while(running) {
        bool redraw = furi_message_queue_get(
                          app->event_queue, &event, AUTOCATCH_LOOP_TIMEOUT_MS) == FuriStatusOk;

        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        if(redraw) {
            switch(event.type) {
            case AutocatchEventTypeInput:
                running = autocatch_handle_input(app, &event.input);
                break;
            case AutocatchEventTypeBtStatus:
                app->bt_status = event.bt_status;
                if(event.bt_status != BtStatusConnected) {
                    pgp_handshake_reset(&app->handshake);
                    app->handshake_started = false;
                    app->press_pending = false;
                }
                break;
            case AutocatchEventTypeGatt:
                autocatch_handle_gatt(app, &event.gatt);
                break;
            }
        }
        if(app->press_pending && (int32_t)(furi_get_tick() - app->press_at) >= 0) {
            app->press_pending = false;
            autocatch_press_button(app);
            redraw = true;
        }
        furi_mutex_release(app->mutex);

        if(redraw) {
            view_port_update(app->view_port);
        }
    }

    if(app->bt) {
        autocatch_bt_stop(app);
    }
    autocatch_free(app);
    return 0;
}
